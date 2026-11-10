#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wtsapi32.h>
#include <comdef.h>
#include <wrl/client.h>
#include "platform/autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <vector>
#include <utility>

namespace {
using Microsoft::WRL::ComPtr;
constexpr auto sourceMarker = "WhatInput.LoginAutostart.v1";
constexpr auto startupArguments = "--autostart --startup-hidden";
constexpr auto runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
_bstr_t bstr(const QString& value) { return _bstr_t(value.toStdWString().c_str()); }
QString systemError(HRESULT hr, const QString& operation) {
    return operation + "：" + QString::fromWCharArray(_com_error(hr).ErrorMessage()) +
           QString("（0x%1）").arg(quint32(hr), 8, 16, QChar('0'));
}
bool failed(HRESULT hr, QString* error, const QString& operation) {
    if (SUCCEEDED(hr)) return false;
    const auto message = systemError(hr, operation);
    if (error) *error = message;
    if (QCoreApplication::instance()) {
        const auto directory = QCoreApplication::applicationDirPath() + "/logs";
        QDir().mkpath(directory); QFile file(directory + "/autostart.log");
        if (file.open(QIODevice::Append | QIODevice::Text)) file.write((QDateTime::currentDateTime().toString(Qt::ISODate) + " " + message + "\n").toUtf8());
    }
    return true;
}
bool missing(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}
QString sidString(PSID sid) {
    LPWSTR value = nullptr;
    if (!ConvertSidToStringSidW(sid, &value)) return {};
    const auto result = QString::fromWCharArray(value); LocalFree(value); return result;
}
QString accountSid(const QString& name) {
    const auto account = name.trimmed();
    if (account.startsWith("S-1-", Qt::CaseInsensitive)) return "S" + account.mid(1);
    DWORD size = 0, domainSize = 0; SID_NAME_USE type{};
    const auto wide = account.toStdWString();
    LookupAccountNameW(nullptr, wide.c_str(), nullptr, &size, nullptr, &domainSize, &type);
    if (!size) return {};
    std::vector<BYTE> sid(size); std::vector<wchar_t> domain(domainSize + 1);
    if (!LookupAccountNameW(nullptr, wide.c_str(), sid.data(), &size, domain.data(), &domainSize, &type)) return {};
    return sidString(sid.data());
}
QString sessionAccountSid() {
    LPWSTR user = nullptr, domain = nullptr; DWORD bytes = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSUserName, &user, &bytes)) return {};
    const auto userName = QString::fromWCharArray(user); WTSFreeMemory(user);
    if (userName.isEmpty()) return {};
    QString domainName;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSDomainName, &domain, &bytes)) {
        domainName = QString::fromWCharArray(domain); WTSFreeMemory(domain);
    }
    return accountSid(domainName.isEmpty() ? userName : domainName + "\\" + userName);
}
QString executablePath() { return QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath(); }
QString taskName() { return "WhatInput-" + wi::currentUserSid(); }
QString normalizedPath(QString value) {
    value = value.trimmed();
    if (value.startsWith('"') && value.endsWith('"')) value = value.mid(1, value.size() - 2);
    if (value.isEmpty()) return {};
    const auto wide = value.toStdWString();
    const auto length = ExpandEnvironmentStringsW(wide.c_str(), nullptr, 0);
    if (!length) return {};
    std::vector<wchar_t> expanded(length);
    if (!ExpandEnvironmentStringsW(wide.c_str(), expanded.data(), length)) return {};
    value = QDir::fromNativeSeparators(QString::fromWCharArray(expanded.data()));
    if (QDir::isRelativePath(value)) return {};
    const QFileInfo file(value);
    const auto canonical = file.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? file.absoluteFilePath() : canonical);
}
bool samePath(const QString& a, const QString& b) {
    const auto left = normalizedPath(a), right = normalizedPath(b);
    return !left.isEmpty() && !right.isEmpty() && left.compare(right, Qt::CaseInsensitive) == 0;
}
bool knownLegacyExecutable(const QString& command) {
    if (samePath(command, executablePath())) return true;
    // Before dev_release existed, this workspace used release/WhatInput.exe.
    QDir directory(QCoreApplication::applicationDirPath());
    if (directory.dirName().compare("dev_release", Qt::CaseInsensitive) == 0 ||
        directory.dirName().compare("release", Qt::CaseInsensitive) == 0) directory.cdUp();
    else if (QRegularExpression("^\\d+\\.\\d+\\.\\d+$").match(directory.dirName()).hasMatch()) {
        directory.cdUp();
        if (directory.dirName().compare("release", Qt::CaseInsensitive) != 0) return false;
        directory.cdUp();
    } else return false;
    if (samePath(command, directory.filePath("release/WhatInput.exe")) ||
        samePath(command, directory.filePath("dev_release/WhatInput.exe"))) return true;
    const auto path = normalizedPath(command), root = normalizedPath(directory.absolutePath());
    if (!path.startsWith(root + '/', Qt::CaseInsensitive)) return false;
    return QRegularExpression("^release/\\d+\\.\\d+\\.\\d+/WhatInput\\.exe$", QRegularExpression::CaseInsensitiveOption)
        .match(path.mid(root.size() + 1)).hasMatch();
}
bool knownStartupArguments(const QString& arguments) {
    const auto values = QProcess::splitCommand(arguments.trimmed());
    return values == QStringList{"--background"} || values == QStringList{"--autostart", "--startup-hidden"};
}
HRESULT taskFolder(ComPtr<ITaskFolder>& root) {
    ComPtr<ITaskService> service;
    auto hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(service.GetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = service->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
    if (FAILED(hr)) return hr;
    return service->GetFolder(_bstr_t(L"\\"), root.GetAddressOf());
}
struct Snapshot {
    wi::AutostartStatus status;
    bool exists = false, owned = false;
    QString xml;
};
struct LegacyEntry {
    bool owned = false, exists = false;
    HRESULT hr = S_OK;
    DWORD type = REG_SZ;
    QString command;
};
LegacyEntry readLegacy() {
    LegacyEntry result;
    DWORD size = 0;
    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
    auto code = RegGetValueW(HKEY_CURRENT_USER, runKey, L"WhatInput", flags, &result.type, nullptr, &size);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return result;
    result.exists = true;
    if (code == ERROR_UNSUPPORTED_TYPE) return result;
    if (code != ERROR_SUCCESS) { result.hr = HRESULT_FROM_WIN32(code); return result; }
    std::vector<wchar_t> data(size / sizeof(wchar_t) + 1, 0);
    code = RegGetValueW(HKEY_CURRENT_USER, runKey, L"WhatInput", flags, &result.type, data.data(), &size);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) { result.exists = false; return result; }
    if (code != ERROR_SUCCESS) { result.hr = HRESULT_FROM_WIN32(code); return result; }
    result.command = QString::fromWCharArray(data.data());
    const auto values = QProcess::splitCommand(result.command);
    result.owned = values.size() >= 2 && knownLegacyExecutable(values.front()) && knownStartupArguments(values.mid(1).join(' '));
    return result;
}
Snapshot readTask(ITaskFolder* root) {
    Snapshot result; ComPtr<IRegisteredTask> task;
    auto hr = root->GetTask(bstr(taskName()), task.GetAddressOf());
    if (missing(hr)) { result.status.state = wi::AutostartState::Disabled; return result; }
    if (FAILED(hr)) { result.status.detail = systemError(hr, "读取登录任务失败"); return result; }
    result.exists = true;
    BSTR raw = nullptr; hr = task->get_Xml(&raw);
    if (FAILED(hr)) { result.status.detail = systemError(hr, "读取任务定义失败"); return result; }
    result.xml = QString::fromWCharArray(raw); SysFreeString(raw);
    QHash<QString, QString> fields; QStringList path; int principals = 0, logons = 0, actions = 0;
    QXmlStreamReader xml(result.xml);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name().toString(); path.append(name);
            if (name == "Principal") ++principals;
            if (path.size() == 3 && path[1] == "Triggers") { if (name == "LogonTrigger") ++logons; else logons += 100; }
            if (path.size() == 3 && path[1] == "Actions") { if (name == "Exec") ++actions; else actions += 100; }
            if (name == "Source" || name == "UserId" || name == "LogonType" || name == "RunLevel" || name == "Command" || name == "Arguments" || name == "WorkingDirectory" || name == "Enabled" || name == "MultipleInstancesPolicy" || name == "DisallowStartIfOnBatteries" || name == "StopIfGoingOnBatteries" || name == "AllowHardTerminate" || name == "StartWhenAvailable" || name == "RunOnlyIfNetworkAvailable" || name == "ExecutionTimeLimit") {
                fields.insert(path.join('/'), xml.readElementText()); path.removeLast();
            }
        } else if (xml.isEndElement() && !path.isEmpty()) path.removeLast();
    }
    if (xml.hasError()) { result.status.detail = "无法解析登录任务定义：" + xml.errorString(); return result; }
    const auto field = [&fields](const char* name) { return fields.value(QString::fromLatin1(name)); };
    const auto user = wi::currentUserSid();
    const auto principalUser = accountSid(field("Task/Principals/Principal/UserId"));
    const auto command = field("Task/Actions/Exec/Command");
    const auto args = field("Task/Actions/Exec/Arguments");
    const auto source = field("Task/RegistrationInfo/Source");
    const auto triggerUser = accountSid(field("Task/Triggers/LogonTrigger/UserId"));
    const bool knownLegacyTask = source.isEmpty() && principals == 1 && logons == 1 && actions == 1 &&
        principalUser == user && triggerUser == user &&
        field("Task/Principals/Principal/LogonType") == "InteractiveToken" &&
        field("Task/Principals/Principal/RunLevel") == "HighestAvailable" &&
        knownLegacyExecutable(command) && knownStartupArguments(args);
    result.owned = source == sourceMarker || knownLegacyTask;
    if (!result.owned) { result.status = {wi::AutostartState::NeedsRepair, "同名任务不属于当前 WhatInput，已拒绝覆盖或删除。"}; return result; }
    VARIANT_BOOL enabled = VARIANT_FALSE; hr = task->get_Enabled(&enabled);
    if (FAILED(hr)) { result.status.detail = systemError(hr, "读取任务启用状态失败"); return result; }
    const auto boolean = [&field](const char* name, bool fallback) {
        const auto value = field(name).trimmed();
        if (value.isEmpty()) return fallback;
        return value == "true" || value == "1";
    };
    if (enabled != VARIANT_TRUE || !boolean("Task/Triggers/LogonTrigger/Enabled", true)) { result.status.state = wi::AutostartState::Disabled; return result; }
    const auto instances = field("Task/Settings/MultipleInstancesPolicy");
    const bool matches = principals == 1 && logons == 1 && actions == 1 && principalUser == user && triggerUser == user && field("Task/Principals/Principal/LogonType") == "InteractiveToken" && field("Task/Principals/Principal/RunLevel") == "HighestAvailable" && samePath(command, executablePath()) && samePath(field("Task/Actions/Exec/WorkingDirectory"), QCoreApplication::applicationDirPath()) && args.trimmed() == startupArguments && (instances.isEmpty() || instances == "IgnoreNew") && field("Task/Settings/ExecutionTimeLimit") == "PT0S" && !boolean("Task/Settings/DisallowStartIfOnBatteries", true) && !boolean("Task/Settings/StopIfGoingOnBatteries", true) && !boolean("Task/Settings/AllowHardTerminate", true) && !boolean("Task/Settings/StartWhenAvailable", false) && !boolean("Task/Settings/RunOnlyIfNetworkAvailable", false);
    result.status = matches ? wi::AutostartStatus{wi::AutostartState::Enabled, {}} : wi::AutostartStatus{wi::AutostartState::NeedsRepair, "登录任务的路径、账户、权限、参数或运行条件不匹配，请保存修复。"};
    return result;
}
QString definition() {
    QString data; QXmlStreamWriter xml(&data); xml.writeStartDocument();
    xml.writeStartElement("Task"); xml.writeDefaultNamespace("http://schemas.microsoft.com/windows/2004/02/mit/task"); xml.writeAttribute("version", "1.2");
    xml.writeStartElement("RegistrationInfo"); xml.writeTextElement("Description", "WhatInput 用户登录自启动"); xml.writeTextElement("Source", sourceMarker); xml.writeEndElement();
    xml.writeStartElement("Triggers"); xml.writeStartElement("LogonTrigger"); xml.writeTextElement("Enabled", "true"); xml.writeTextElement("UserId", wi::currentUserSid()); xml.writeEndElement(); xml.writeEndElement();
    xml.writeStartElement("Principals"); xml.writeStartElement("Principal"); xml.writeAttribute("id", "Author"); xml.writeTextElement("UserId", wi::currentUserSid()); xml.writeTextElement("LogonType", "InteractiveToken"); xml.writeTextElement("RunLevel", "HighestAvailable"); xml.writeEndElement(); xml.writeEndElement();
    xml.writeStartElement("Settings");
    for (const auto& pair : {std::pair{"MultipleInstancesPolicy", "IgnoreNew"}, {"DisallowStartIfOnBatteries", "false"}, {"StopIfGoingOnBatteries", "false"}, {"AllowHardTerminate", "false"}, {"StartWhenAvailable", "false"}, {"RunOnlyIfNetworkAvailable", "false"}, {"Enabled", "true"}, {"ExecutionTimeLimit", "PT0S"}}) xml.writeTextElement(pair.first, pair.second);
    xml.writeEndElement(); xml.writeStartElement("Actions"); xml.writeAttribute("Context", "Author"); xml.writeStartElement("Exec");
    xml.writeTextElement("Command", QDir::toNativeSeparators(executablePath())); xml.writeTextElement("Arguments", startupArguments); xml.writeTextElement("WorkingDirectory", QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndDocument(); return data;
}
bool validStartupPath(QString* error) {
    const auto exe = executablePath();
    if (exe.isEmpty() || !QFileInfo(exe).isFile() || !QFileInfo(QCoreApplication::applicationDirPath()).isDir()) {
        if (error) *error = QString::fromUtf8("\xE6\x97\xA0\xE6\xB3\x95\xE5\xAE\x9A\xE4\xBD\x8D\xE8\x87\xAA\xE5\x90\xAF\xE7\xA8\x8B\xE5\xBA\x8F\xE6\x88\x96\xE5\xB7\xA5\xE4\xBD\x9C\xE7\x9B\xAE\xE5\xBD\x95\xE3\x80\x82");
        return false;
    }
    return true;
}
bool cleanLegacy(const LegacyEntry& before, bool& removed, QString* error) {
    removed = false;
    if (!before.owned) return true;
    const auto current = readLegacy();
    if (failed(current.hr, error, "读取旧自启配置失败")) return false;
    if (!current.exists) return true;
    if (current.command != before.command || current.type != before.type) {
        if (error) *error = "旧自启项在保存期间被修改，请重新打开设置后重试。";
        return false;
    }
    HKEY key{};
    auto code = RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &key);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
    if (code == ERROR_SUCCESS) { code = RegDeleteValueW(key, L"WhatInput"); RegCloseKey(key); }
    if (code == ERROR_FILE_NOT_FOUND) return true;
    if (failed(HRESULT_FROM_WIN32(code), error, "清理旧自启配置失败")) return false;
    removed = true; return true;
}
bool restoreLegacy(const LegacyEntry& before, QString* error) {
    const auto current = readLegacy();
    if (failed(current.hr, error, "读取待恢复的自启项失败")) return false;
    if (current.exists) {
        if (current.command == before.command && current.type == before.type) return true;
        if (error) *error = "旧自启项已由其他操作写入，未覆盖其内容。";
        return false;
    }
    HKEY key{}; auto code = RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (code == ERROR_SUCCESS) {
        const auto value = before.command.toStdWString();
        code = RegSetValueExW(key, L"WhatInput", 0, before.type, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    return !failed(HRESULT_FROM_WIN32(code), error, "恢复旧自启项失败");
}
}
namespace wi {
QString currentUserSid() {
    HANDLE token{}; if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size); std::vector<BYTE> data(size);
    const bool ok = GetTokenInformation(token, TokenUser, data.data(), size, &size) != FALSE; CloseHandle(token);
    return ok ? sidString(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid) : QString{};
}
bool administratorSession(QString* error) {
    HANDLE token{}; TOKEN_ELEVATION elevation{}; DWORD size = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return !failed(HRESULT_FROM_WIN32(GetLastError()), error, "读取管理员权限失败");
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != FALSE; const auto code = GetLastError(); CloseHandle(token);
    if (!ok) return !failed(HRESULT_FROM_WIN32(code), error, "读取管理员权限失败");
    if (!elevation.TokenIsElevated) { if (error) *error = "WhatInput 需要管理员授权，请重新启动并允许 Windows 权限请求。"; return false; }
    const auto desktop = sessionAccountSid();
    if (desktop.isEmpty() || desktop != currentUserSid()) { if (error) *error = "此版本仅支持管理员账户本人登录。暂不支持标准用户输入其他管理员凭据运行，请使用管理员账户登录 Windows 后运行。"; return false; }
    return true;
}
bool initializeTaskSecurity(QString* error) {
    ComScope scope; if (scope.hr != RPC_E_CHANGED_MODE && failed(scope.hr, error, "初始化 COM 失败")) return false;
    const auto hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    return !failed(hr, error, "初始化任务计划程序安全上下文失败");
}
AutostartStatus autoStartStatus() {
    ComScope scope;
    if (FAILED(scope.hr) && scope.hr != RPC_E_CHANGED_MODE) return {AutostartState::Unknown, systemError(scope.hr, "初始化 COM 失败")};
    ComPtr<ITaskFolder> root; const auto hr = taskFolder(root);
    if (FAILED(hr)) return {AutostartState::Unknown, systemError(hr, "连接任务计划程序失败")};
    const auto task = readTask(root.Get());
    if (task.status.state == AutostartState::Unknown || (task.exists && !task.owned)) return task.status;
    const auto legacy = readLegacy();
    if (FAILED(legacy.hr)) return {AutostartState::Unknown, systemError(legacy.hr, "读取旧自启配置失败")};
    if (legacy.owned) return {AutostartState::NeedsRepair, "检测到旧 Run 自启项；开启时迁移为登录任务，取消时一并清理。"};
    return task.status;
}
bool setAutoStartEnabled(bool enabled, QString* error) {
    if (!administratorSession(error)) return false;
    ComScope scope; if (scope.hr != RPC_E_CHANGED_MODE && failed(scope.hr, error, "初始化 COM 失败")) return false;
    ComPtr<ITaskFolder> root; if (failed(taskFolder(root), error, "连接任务计划程序失败")) return false;
    const auto before = readTask(root.Get());
    if (before.status.state == AutostartState::Unknown || (before.exists && !before.owned)) { if (error) *error = before.status.detail; return false; }
    const auto legacyBefore = readLegacy();
    if (failed(legacyBefore.hr, error, "读取旧自启配置失败")) return false;
    const auto registerXml = [&root](const QString& xml) {
        ComPtr<IRegisteredTask> task;
        return root->RegisterTask(bstr(taskName()), bstr(xml), TASK_CREATE_OR_UPDATE, _variant_t(currentUserSid().toStdWString().c_str()), _variant_t(), TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(), task.GetAddressOf());
    };
    if (enabled) {
        if (!validStartupPath(error)) return false;
        if (failed(registerXml(definition()), error, "注册登录任务失败")) return false;
    } else if (before.exists) {
        const auto hr = root->DeleteTask(bstr(taskName()), 0);
        if (!missing(hr) && failed(hr, error, "删除登录任务失败")) return false;
    }
    bool legacyRemoved = false;
    const bool cleaned = cleanLegacy(legacyBefore, legacyRemoved, error);
    const auto after = readTask(root.Get());
    const auto legacyAfter = readLegacy();
    const bool valid = (enabled ? after.status.state == AutostartState::Enabled : !after.exists && after.status.state == AutostartState::Disabled) && SUCCEEDED(legacyAfter.hr) && !legacyAfter.owned;
    if (cleaned && valid) return true;
    if (cleaned && !valid && error) *error = "保存后自启状态回读校验失败：" + after.status.detail + (FAILED(legacyAfter.hr) ? systemError(legacyAfter.hr, "读取旧自启项失败") : QString{});
    // Do not overwrite a task another program replaced while the dialog was saving.
    if (after.status.state != AutostartState::Unknown && (!after.exists || after.owned)) {
        const auto rollback = before.exists ? registerXml(before.xml) : root->DeleteTask(bstr(taskName()), 0);
        if (FAILED(rollback) && !missing(rollback) && error) *error += "\n" + systemError(rollback, "恢复原任务失败");
    } else if (error) *error += "\n任务状态未知或已被替换，未覆盖其他程序的任务。";
    if (legacyRemoved) {
        QString restoreError;
        if (!restoreLegacy(legacyBefore, &restoreError) && error) *error += "\n" + restoreError;
    }
    return false;
}
}
