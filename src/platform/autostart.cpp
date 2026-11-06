#include "platform/autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <vector>
#include <windows.h>

namespace {
constexpr auto runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
QString startupCommand() {
    return "\"" +
           QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
           "\" --background";
}
} // namespace
namespace wi {
bool autoStartEnabled() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, runKey, L"WhatInput", RRF_RT_REG_SZ,
                     nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t))
        return false;
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, runKey, L"WhatInput", RRF_RT_REG_SZ,
                     nullptr, value.data(), &size) != ERROR_SUCCESS)
        return false;
    return QString::fromWCharArray(value.data())
               .compare(startupCommand(), Qt::CaseInsensitive) == 0;
}
bool setAutoStartEnabled(bool enabled) {
    HKEY key{};
    const auto opened =
        RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS)
        return false;
    LONG result;
    if (enabled) {
        const auto command = startupCommand().toStdWString();
        result = RegSetValueExW(key, L"WhatInput", 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(command.c_str()),
                                DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"WhatInput");
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
} // namespace wi
