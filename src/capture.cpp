// clang-format off
#include <windows.h>
#include <dbt.h>
#include <windowsx.h>
#include <xinput.h>
#include <tlhelp32.h>
// clang-format on
#include "capture.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <future>
#include <thread>

namespace {
namespace fs = std::filesystem;
wi::Engine *service = nullptr;
std::atomic<bool> stopping = false, sleeping = false;
std::atomic<unsigned> generation = 0;
std::atomic<HWND> window = nullptr;
std::thread inputThread, padThread;
std::function<void()> showCallback, exitCallback;
bool diagnostics = false;
ULONGLONG diagnosticStarted = 0;
HHOOK diagnosticKeyboardHook = nullptr, diagnosticMouseHook = nullptr;
DWORD keyboardHookError = 0, mouseHookError = 0;
struct DiagnosticCounts {
    quint64 rawKeyDown = 0, rawKeyUp = 0, rawMouseDown = 0, rawMouseUp = 0;
    quint64 hookKeyDown = 0, hookKeyUp = 0, hookMouseDown = 0, hookMouseUp = 0;
};
std::map<DWORD, DiagnosticCounts> diagnosticCounts;
static DiagnosticCounts &foregroundCounts() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return diagnosticCounts[pid];
}
static LRESULT CALLBACK diagnosticKeyboard(int code, WPARAM message,
                                           LPARAM data) {
    if (code == HC_ACTION) {
        auto &counts = foregroundCounts();
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)
            ++counts.hookKeyDown;
        if (message == WM_KEYUP || message == WM_SYSKEYUP)
            ++counts.hookKeyUp;
    }
    return CallNextHookEx(nullptr, code, message, data);
}
static LRESULT CALLBACK diagnosticMouse(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
            ++foregroundCounts().hookMouseDown;
            break;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
            ++foregroundCounts().hookMouseUp;
            break;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}
static void endDiagnosticHooks() {
    if (diagnosticKeyboardHook)
        UnhookWindowsHookEx(diagnosticKeyboardHook);
    if (diagnosticMouseHook)
        UnhookWindowsHookEx(diagnosticMouseHook);
    diagnosticKeyboardHook = nullptr;
    diagnosticMouseHook = nullptr;
}
std::atomic<quint64> acceptedKeyboard = 0, acceptedMouse = 0, acceptedPad = 0;
quint64 rawKeyboard = 0, rawMouse = 0, rawErrors = 0, keyDown = 0, keyUp = 0,
        mouseDown = 0, mouseUp = 0;
static std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string o(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), o.data(), n,
                        nullptr, nullptr);
    return o;
}
static bool fullscreenAt(HWND h, POINT point) {
    if (!h || IsIconic(h) || !IsWindowVisible(h))
        return false;
    // Client bounds exclude borders/title bars: a maximized normal window is
    // not mistaken for a fullscreen game. Use the monitor, not the work area.
    RECT client{};
    if (!GetClientRect(h, &client))
        return false;
    POINT origin{client.left, client.top}, end{client.right, client.bottom};
    if (!ClientToScreen(h, &origin) || !ClientToScreen(h, &end))
        return false;
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST),
                         &monitor))
        return false;
    return origin.x <= monitor.rcMonitor.left &&
           origin.y <= monitor.rcMonitor.top &&
           end.x >= monitor.rcMonitor.right &&
           end.y >= monitor.rcMonitor.bottom && point.x >= origin.x &&
           point.x < end.x && point.y >= origin.y && point.y < end.y;
}
static std::wstring executableName(DWORD pid) {
    // Process names only: no module/heap snapshot, game memory or elevated
    // rights. Protected processes can deny a full-path query while their
    // executable names remain available in the normal Windows process list.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::wstring name;
    if (Process32FirstW(snapshot, &entry))
        do {
            if (entry.th32ProcessID == pid) {
                name = entry.szExeFile;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    std::transform(name.begin(), name.end(), name.begin(), towlower);
    return name;
}
static std::pair<std::string, std::string> application(bool mouse) {
    HWND h = GetForegroundWindow();
    if (mouse) {
        POINT p{};
        if (GetCursorPos(&p) && !fullscreenAt(h, p)) {
            if (auto t = WindowFromPoint(p))
                h = GetAncestor(t, GA_ROOT);
        }
    }
    wchar_t cls[128]{};
    GetClassNameW(h, cls, 128);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"Shell_TrayWnd") ||
        !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return {"system", "Windows 系统"};
    DWORD pid{};
    GetWindowThreadProcessId(h, &pid);
    static thread_local DWORD oldPid = 0;
    static thread_local HWND oldWindow = nullptr;
    static thread_local std::pair<std::string, std::string> cached;
    static thread_local ULONGLONG cachedAt = 0;
    static thread_local ULONGLONG cacheDuration = 1000;
    if (pid == oldPid && h == oldWindow &&
        GetTickCount64() - cachedAt < cacheDuration)
        return cached;
    std::pair<std::string, std::string> a = {"unknown", "未知应用"};
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p) {
        wchar_t file[32768];
        DWORD n = 32768;
        if (QueryFullProcessImageNameW(p, 0, file, &n)) {
            std::wstring v(file, n);
            std::transform(v.begin(), v.end(), v.begin(), towlower);
            auto name = fs::path(v).filename().wstring();
            if (name == L"startmenuexperiencehost.exe" ||
                name == L"shellexperiencehost.exe")
                a = {"system", "Windows 系统"};
            else
                a = {utf8(v),
                     name == L"explorer.exe" ? "文件资源管理器" : utf8(name)};
        }
        CloseHandle(p);
    }
    cacheDuration = 1000;
    if (a.first == "unknown" && pid) {
        const auto name = executableName(pid);
        if (!name.empty()) {
            if (name == L"startmenuexperiencehost.exe" ||
                name == L"shellexperiencehost.exe")
                a = {"system", "Windows 系统"};
            else
                a = {"exe-name:" + utf8(name), utf8(name) + "（进程路径受限）"};
        }
        // Avoid querying a protected process / taking a snapshot on each key.
        cacheDuration = 5000;
    }
    oldPid = pid;
    oldWindow = h;
    cached = a;
    cachedAt = GetTickCount64();
    return a;
}

static void add(const std::string &, const std::string &type,
                const std::string &, const std::string &control,
                const std::string &name, int count, bool mouse) {
    if (count <= 0 || sleeping || stopping)
        return;
    const auto app = application(mouse);
    service->record(type, control, name, app.first, app.second, count);
    if (type == "keyboard")
        acceptedKeyboard += count;
    else if (type == "mouse")
        acceptedMouse += count;
    else
        acceptedPad += count;
}
struct Device {
    std::string id, name, type;
    wi::State state;
};
static std::map<HANDLE, Device> devices;
// Handles are ephemeral and used only for independent edge/wheel state.
static Device &device(HANDLE handle, bool mouse) {
    auto i = devices.find(handle);
    if (i != devices.end())
        return i->second;
    const std::string type = mouse ? "mouse" : "keyboard";
    return devices
        .emplace(handle,
                 Device{wi::categoryId(type), wi::categoryName(type), type, {}})
        .first->second;
}
static void rawInput(HRAWINPUT h) {
    UINT n = 0;
    if (GetRawInputData(h, RID_INPUT, nullptr, &n, sizeof(RAWINPUTHEADER)) !=
        0) {
        ++rawErrors;
        return;
    }
    if (n < sizeof(RAWINPUTHEADER)) {
        ++rawErrors;
        return;
    }
    std::vector<BYTE> b(n);
    if (GetRawInputData(h, RID_INPUT, b.data(), &n, sizeof(RAWINPUTHEADER)) !=
        n) {
        ++rawErrors;
        return;
    }
    auto &r = *reinterpret_cast<RAWINPUT *>(b.data());
    if (r.header.dwType == RIM_TYPEKEYBOARD) {
        ++rawKeyboard;
        auto &k = r.data.keyboard;
        if (k.Flags & RI_KEY_BREAK)
            ++keyUp;
        else
            ++keyDown;
        if (diagnostics) {
            auto &counts = foregroundCounts();
            if (k.Flags & RI_KEY_BREAK)
                ++counts.rawKeyUp;
            else
                ++counts.rawKeyDown;
        }
        if (k.VKey == 255 || k.MakeCode == 0xff)
            return;
        if ((k.Flags & RI_KEY_E0) && (k.MakeCode == 0x2a || k.MakeCode == 0x36))
            return;
        auto &d = device(r.header.hDevice, false);
        unsigned scan = k.MakeCode;
        if (!scan)
            scan = MapVirtualKeyW(k.VKey, MAPVK_VK_TO_VSC);
        char code[64];
        snprintf(code, sizeof(code), "kbd:%04X:%u", scan,
                 k.Flags & (RI_KEY_E0 | RI_KEY_E1));
        bool pressed = !(k.Flags & RI_KEY_BREAK);
        if (d.state.edge(code, pressed)) {
            wchar_t label[128]{};
            LONG packed = LONG(scan << 16);
            if (k.Flags & RI_KEY_E0)
                packed |= 1 << 24;
            GetKeyNameTextW(packed, label, 128);
            std::string name = utf8(label);
            if (scan == 0x1c)
                name = (k.Flags & RI_KEY_E0) ? "小键盘 Enter" : "Enter";
            if (name.empty())
                name = code;
            add(d.id, d.type, d.name, code, name, 1, false);
        }
        if (k.VKey == VK_PAUSE)
            d.state.edge(code, false);
    } else if (r.header.dwType == RIM_TYPEMOUSE) {
        ++rawMouse;
        auto &d = device(r.header.hDevice, true);
        auto &m = r.data.mouse;
        const char *labels[] = {"左键", "右键", "中键", "侧键 1", "侧键 2"};
        for (int i = 0; i < 5; ++i) {
            if (m.usButtonFlags & (1 << (i * 2)))
                ++mouseDown;
            if (m.usButtonFlags & (2 << (i * 2)))
                ++mouseUp;
            if (diagnostics && (m.usButtonFlags & (3 << (i * 2)))) {
                auto &counts = foregroundCounts();
                if (m.usButtonFlags & (1 << (i * 2)))
                    ++counts.rawMouseDown;
                if (m.usButtonFlags & (2 << (i * 2)))
                    ++counts.rawMouseUp;
            }
            auto c = "mouse:button:" + std::to_string(i);
            if (m.usButtonFlags & (1 << (i * 2)))
                if (d.state.edge(c, true))
                    add(d.id, d.type, d.name, c, labels[i], 1, true);
            if (m.usButtonFlags & (2 << (i * 2)))
                d.state.edge(c, false);
        }
        for (int a = 0; a < 2; ++a)
            if (m.usButtonFlags & (a ? RI_MOUSE_HWHEEL : RI_MOUSE_WHEEL)) {
                int delta = static_cast<SHORT>(m.usButtonData);
                int count = d.state.scroll(a, delta);
                auto c = "mouse:wheel:" + std::to_string(a) + ":" +
                         (delta < 0 ? "-" : "+");
                const char *label = a ? (delta < 0 ? "滚轮向左" : "滚轮向右")
                                      : (delta < 0 ? "滚轮向下" : "滚轮向上");
                add(d.id, d.type, d.name, c, label, count, true);
            }
    }
}
static void gamepads() {
    XINPUT_STATE previous[4]{};
    bool connected[4]{}, lt[4]{}, rt[4]{};
    ULONGLONG probe[4]{};
    unsigned seen = 0;
    const WORD masks[] = {XINPUT_GAMEPAD_DPAD_UP,
                          XINPUT_GAMEPAD_DPAD_DOWN,
                          XINPUT_GAMEPAD_DPAD_LEFT,
                          XINPUT_GAMEPAD_DPAD_RIGHT,
                          XINPUT_GAMEPAD_START,
                          XINPUT_GAMEPAD_BACK,
                          XINPUT_GAMEPAD_LEFT_THUMB,
                          XINPUT_GAMEPAD_RIGHT_THUMB,
                          XINPUT_GAMEPAD_LEFT_SHOULDER,
                          XINPUT_GAMEPAD_RIGHT_SHOULDER,
                          XINPUT_GAMEPAD_A,
                          XINPUT_GAMEPAD_B,
                          XINPUT_GAMEPAD_X,
                          XINPUT_GAMEPAD_Y};
    const char *names[] = {"十字键上", "十字键下", "十字键左",   "十字键右",
                           "Start",    "Back",     "左摇杆按压", "右摇杆按压",
                           "LB",       "RB",       "A",          "B",
                           "X",        "Y"};
    while (!stopping) {
        if (seen != generation) {
            std::fill(std::begin(connected), std::end(connected), false);
            seen = generation;
        }
        for (DWORD i = 0; i < 4; ++i) {
            auto now = GetTickCount64();
            if (!connected[i] && now < probe[i])
                continue;
            auto id = "xinput:" + std::to_string(i);
            auto name = "手柄 " + std::to_string(i + 1) + "（插槽 " +
                        std::to_string(i) + "）";
            XINPUT_STATE s{};
            if (XInputGetState(i, &s) != ERROR_SUCCESS) {
                connected[i] = false;
                probe[i] = now + 1000;
                continue;
            }
            if (connected[i] && !sleeping) {
                for (int j = 0; j < 14; ++j)
                    if ((s.Gamepad.wButtons & masks[j]) &&
                        !(previous[i].Gamepad.wButtons & masks[j]))
                        add(id, "gamepad", name,
                            "pad:" + std::to_string(masks[j]), names[j], 1,
                            false);
                bool l = wi::trigger(lt[i], s.Gamepad.bLeftTrigger),
                     r = wi::trigger(rt[i], s.Gamepad.bRightTrigger);
                if (l && !lt[i])
                    add(id, "gamepad", name, "pad:LT", "LT", 1, false);
                if (r && !rt[i])
                    add(id, "gamepad", name, "pad:RT", "RT", 1, false);
                lt[i] = l;
                rt[i] = r;
            } else {
                lt[i] = s.Gamepad.bLeftTrigger > 96;
                rt[i] = s.Gamepad.bRightTrigger > 96;
            }
            previous[i] = s;
            connected[i] = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

static void diagnosticSnapshot(HWND receiver) {
    // Opt-in aggregate health snapshot only. No individual key identities,
    // input text, pointer positions, or per-event timestamps are written.
    DWORD pid = 0;
    HWND foreground = GetForegroundWindow();
    GetWindowThreadProcessId(foreground, &pid);
    auto app = application(false);
    auto elevation = [](DWORD processId) {
        QJsonObject result;
        HANDLE process =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!process) {
            result["error"] = double(GetLastError());
            return result;
        }
        HANDLE token = nullptr;
        if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
            TOKEN_ELEVATION value{};
            DWORD bytes = 0;
            if (GetTokenInformation(token, TokenElevation, &value,
                                    sizeof(value), &bytes))
                result["elevated"] = value.TokenIsElevated != 0;
            else
                result["error"] = double(GetLastError());
            CloseHandle(token);
        } else
            result["error"] = double(GetLastError());
        CloseHandle(process);
        return result;
    };
    QJsonArray registrations;
    UINT size = 0;
    UINT rc =
        GetRegisteredRawInputDevices(nullptr, &size, sizeof(RAWINPUTDEVICE));
    DWORD registrationError = rc == UINT(-1) ? GetLastError() : 0;
    if (!registrationError && size) {
        std::vector<RAWINPUTDEVICE> entries(size);
        rc = GetRegisteredRawInputDevices(entries.data(), &size,
                                          sizeof(RAWINPUTDEVICE));
        if (rc == UINT(-1))
            registrationError = GetLastError();
        else
            for (UINT i = 0; i < rc; ++i)
                registrations.append(QJsonObject{
                    {"page", entries[i].usUsagePage},
                    {"usage", entries[i].usUsage},
                    {"flags", double(entries[i].dwFlags)},
                    {"ourWindow", entries[i].hwndTarget == receiver}});
    }
    int held = 0;
    for (auto &[handle, d] : devices)
        held += int(d.state.down.size());
    QJsonArray perProcess;
    for (const auto &[id, c] : diagnosticCounts) {
        perProcess.append(
            QJsonObject{{"pid", double(id)},
                        {"name", QString::fromStdWString(executableName(id))},
                        {"rawKeyDown", double(c.rawKeyDown)},
                        {"rawKeyUp", double(c.rawKeyUp)},
                        {"rawMouseDown", double(c.rawMouseDown)},
                        {"rawMouseUp", double(c.rawMouseUp)},
                        {"hookKeyDown", double(c.hookKeyDown)},
                        {"hookKeyUp", double(c.hookKeyUp)},
                        {"hookMouseDown", double(c.hookMouseDown)},
                        {"hookMouseUp", double(c.hookMouseUp)}});
    }
    QJsonObject snapshot{
        {"updated", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"complete", GetTickCount64() - diagnosticStarted >= 30000},
        {"keyboardHookInstalled", diagnosticKeyboardHook != nullptr},
        {"mouseHookInstalled", diagnosticMouseHook != nullptr},
        {"keyboardHookError", double(keyboardHookError)},
        {"mouseHookError", double(mouseHookError)},
        {"perForegroundProcess", perProcess},
        {"processId", double(GetCurrentProcessId())},
        {"foregroundPid", double(pid)},
        {"collectorElevation", elevation(GetCurrentProcessId())},
        {"foregroundElevation", elevation(pid)},
        {"foregroundApp", QString::fromStdString(app.first)},
        {"foregroundName", QString::fromStdString(app.second)},
        {"rawKeyboard", double(rawKeyboard)},
        {"rawMouse", double(rawMouse)},
        {"keyDown", double(keyDown)},
        {"keyUp", double(keyUp)},
        {"mouseDown", double(mouseDown)},
        {"mouseUp", double(mouseUp)},
        {"acceptedKeyboard", double(acceptedKeyboard.load())},
        {"acceptedMouse", double(acceptedMouse.load())},
        {"acceptedPad", double(acceptedPad.load())},
        {"rawErrors", double(rawErrors)},
        {"heldControls", held},
        {"sleeping", sleeping.load()},
        {"registrationError", double(registrationError)},
        {"registrations", registrations}};
    const auto directory = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(directory);
    QSaveFile file(directory + "/capture-diagnostics.json");
    if (file.open(QIODevice::WriteOnly)) {
        auto data = QJsonDocument(snapshot).toJson();
        if (file.write(data) == data.size())
            file.commit();
    }
}
static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INPUT:
        rawInput(reinterpret_cast<HRAWINPUT>(l));
        break;
    case WM_TIMER:
        if (diagnostics && w == 1) {
            diagnosticSnapshot(h);
            if (GetTickCount64() - diagnosticStarted >= 30000) {
                KillTimer(h, 1);
                endDiagnosticHooks();
                diagnostics = false;
            }
        }
        break;
    case WM_INPUT_DEVICE_CHANGE:
        if (w == GIDC_REMOVAL)
            devices.erase(reinterpret_cast<HANDLE>(l));
        break;
    case WM_POWERBROADCAST:
        if (w == PBT_APMSUSPEND) {
            sleeping = true;
            service->flush();
        }
        if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) {
            ++generation;
            sleeping = false;
        }
        return TRUE;
    case WM_APP + 2:
        if (showCallback)
            showCallback();
        return 0;
    case WM_CLOSE:
        if (exitCallback)
            exitCallback();
        return 0;
    case WM_APP + 10:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        endDiagnosticHooks();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace
namespace wi {
bool startCapture(Engine &engine, std::function<void()> showWindow,
                  std::function<void()> requestExit) {
    service = &engine;
    showCallback = std::move(showWindow);
    exitCallback = std::move(requestExit);
    stopping = false;
    sleeping = false;
    devices.clear();
    diagnostics =
        QCoreApplication::arguments().contains("--capture-diagnostics");
    diagnosticCounts.clear();
    keyboardHookError = mouseHookError = 0;
    rawKeyboard = rawMouse = rawErrors = keyDown = keyUp = mouseDown = mouseUp =
        0;
    acceptedKeyboard = 0;
    acceptedMouse = 0;
    acceptedPad = 0;
    std::promise<bool> ready;
    auto result = ready.get_future();
    inputThread = std::thread([ready = std::move(ready)]() mutable {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = instance;
        cls.lpszClassName = L"WhatInputCollector";
        RegisterClassW(&cls);
        HWND h = CreateWindowW(cls.lpszClassName, L"WhatInput capture", 0, 0, 0,
                               0, 0, nullptr, nullptr, instance, nullptr);
        window = h;
        if (!h) {
            service->inputError(QStringLiteral("无法创建输入采集窗口"));
            ready.set_value(false);
            return;
        }
        RAWINPUTDEVICE raw[] = {{1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, h},
                                {1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, h}};
        if (!RegisterRawInputDevices(raw, 2, sizeof(raw[0]))) {
            service->inputError(QStringLiteral("Raw Input 注册失败：") +
                                QString::number(GetLastError()));
            DestroyWindow(h);
            window = nullptr;
            ready.set_value(false);
            return;
        }
        if (diagnostics) {
            // Diagnostic callbacks only count messages and always pass them on.
            // They never feed production counts or inspect input contents.
            diagnosticKeyboardHook = SetWindowsHookExW(
                WH_KEYBOARD_LL, diagnosticKeyboard, instance, 0);
            if (!diagnosticKeyboardHook)
                keyboardHookError = GetLastError();
            diagnosticMouseHook =
                SetWindowsHookExW(WH_MOUSE_LL, diagnosticMouse, instance, 0);
            if (!diagnosticMouseHook)
                mouseHookError = GetLastError();
            diagnosticStarted = GetTickCount64();
            if (!SetTimer(h, 1, 1000, nullptr)) {
                endDiagnosticHooks();
                diagnostics = false;
                service->inputError(QStringLiteral("无法启动诊断计时器"));
            }
        }
        ready.set_value(true);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        RAWINPUTDEVICE remove[] = {{1, 6, RIDEV_REMOVE, nullptr},
                                   {1, 2, RIDEV_REMOVE, nullptr}};
        RegisterRawInputDevices(remove, 2, sizeof(remove[0]));
        window = nullptr;
    });
    if (!result.get()) {
        if (inputThread.joinable())
            inputThread.join();
        return false;
    }
    padThread = std::thread(gamepads);
    return true;
}
void stopCapture() {
    stopping = true;
    if (auto h = window.load())
        PostMessageW(h, WM_APP + 10, 0, 0);
    if (inputThread.joinable())
        inputThread.join();
    if (padThread.joinable())
        padThread.join();
    devices.clear();
}
} // namespace wi
