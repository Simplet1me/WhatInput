// Windows SDK dependent headers must follow windows.h.
// clang-format off
#include <windows.h>
#include "core.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <dbt.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shellapi.h>
#include <thread>
#include <windowsx.h>
#include <xinput.h>
// clang-format on

namespace fs = std::filesystem;
static fs::path root;
static HWND window;
static std::mutex guard;
static std::condition_variable wake;
static wi::Buffer active;
static std::atomic<bool> stopping = false, diskStopping = false,
                         sleeping = false;
static bool clearRequested = false;
static std::atomic<unsigned> generation = 0;
static std::string lastError;
static wi::Count lastCommit = 0;
static NOTIFYICONDATAW tray{};
static UINT taskbarMessage;
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
static std::string path8(const fs::path &p) { return utf8(p.wstring()); }
static void logError(const std::string &e) {
    try {
        auto p = root / L"logs/collector.log";
        if (fs::exists(p) && fs::file_size(p) > 1024 * 1024) {
            fs::remove(root / L"logs/collector.old.log");
            fs::rename(p, root / L"logs/collector.old.log");
        }
        std::ofstream(p, std::ios::app) << wi::nowSecond() << " " << e << "\n";
    } catch (...) {
    }
}
static std::pair<std::string, std::string> application(bool mouse) {
    HWND h = GetForegroundWindow();
    if (mouse) {
        POINT p{};
        GetCursorPos(&p);
        if (auto t = WindowFromPoint(p))
            h = GetAncestor(t, GA_ROOT);
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
    static thread_local std::pair<std::string, std::string> cached;
    static thread_local ULONGLONG cachedAt = 0;
    if (pid == oldPid && GetTickCount64() - cachedAt < 1000)
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
    oldPid = pid;
    cached = a;
    cachedAt = GetTickCount64();
    return a;
}
static void add(const std::string & /*device*/, const std::string &type,
                const std::string &deviceName, const std::string &control,
                const std::string &name, int count, bool mouse) {
    if (count <= 0 || sleeping)
        return;
    auto app = application(mouse);
    wi::Key k{wi::nowSecond(), wi::categoryId(type), type, control, app.first};
    std::lock_guard lock(guard);
    if (active.size() > 500000) {
        lastError = "待写数据达到上限，采集故障；请检查磁盘";
        return;
    }
    auto [it, newEntry] =
        active.try_emplace(k, wi::Entry{k, deviceName, name, app.second, 0});
    it->second.count += count;
}
static void writer() {
    std::unique_ptr<wi::Database> db;
    wi::Buffer pending;
    std::string batch;
    while (true) {
        {
            std::unique_lock lock(guard);
            if (!diskStopping)
                wake.wait_for(lock, std::chrono::seconds(5), [] {
                    return diskStopping.load() || clearRequested;
                });
            if (clearRequested) {
                try {
                    if (!db)
                        db = std::make_unique<wi::Database>(
                            path8(root / L"data/whatinput.sqlite3"));
                    db->clear();
                    active.clear();
                    pending.clear();
                    batch.clear();
                    lastError.clear();
                    lastCommit = wi::nowSecond();
                } catch (const std::exception &e) {
                    lastError = e.what();
                    logError(e.what());
                }
                clearRequested = false;
                wake.notify_all();
            }
            if (pending.empty()) {
                pending.swap(active);
                batch = wi::uniqueBatch();
            }
        }
        try {
            if (!db)
                db = std::make_unique<wi::Database>(
                    path8(root / L"data/whatinput.sqlite3"));
            if (!pending.empty())
                db->commit(batch, pending);
            pending.clear();
            std::lock_guard lock(guard);
            lastCommit = wi::nowSecond();
            lastError.clear();
        } catch (const std::exception &e) {
            std::lock_guard lock(guard);
            lastError = e.what();
            logError(e.what());
            db.reset();
        }
        if (diskStopping) {
            std::lock_guard lock(guard);
            if (!pending.empty()) {
                MessageBoxW(
                    nullptr,
                    L"最后一批数据保存失败，请查看 logs/collector.log。",
                    L"WhatInput", MB_ICONERROR);
                break;
            }
            if (active.empty())
                break;
        }
    }
}
struct Device {
    std::string id, name, type;
    wi::State state;
};
static std::map<HANDLE, Device> devices;
// Handles are ephemeral and used only for independent edge/wheel state.
static Device &device(HANDLE handle, bool mouse) {
    auto i = devices.find(handle);
    if (i != devices.end()) return i->second;
    const std::string type = mouse ? "mouse" : "keyboard";
    return devices.emplace(handle, Device{wi::categoryId(type),
        wi::categoryName(type), type, {}}).first->second;
}
static void rawInput(HRAWINPUT h) {
    UINT n = 0;
    if (GetRawInputData(h, RID_INPUT, nullptr, &n, sizeof(RAWINPUTHEADER)) != 0)
        return;
    std::vector<BYTE> b(n);
    if (GetRawInputData(h, RID_INPUT, b.data(), &n, sizeof(RAWINPUTHEADER)) !=
        n)
        return;
    auto &r = *reinterpret_cast<RAWINPUT *>(b.data());
    if (r.header.dwType == RIM_TYPEKEYBOARD) {
        auto &k = r.data.keyboard;
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
        auto &d = device(r.header.hDevice, true);
        auto &m = r.data.mouse;
        const char *labels[] = {"左键", "右键", "中键", "侧键 1", "侧键 2"};
        for (int i = 0; i < 5; ++i) {
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
static void openStats() {
    ShellExecuteW(nullptr, L"open", (root / L"WhatInputStats.exe").c_str(),
                  nullptr, root.c_str(), SW_SHOWNORMAL);
}
static bool autostart() {
    wchar_t b[32768];
    DWORD bytes = sizeof(b);
    return RegGetValueW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        L"WhatInput", RRF_RT_REG_SZ, nullptr, b,
                        &bytes) == ERROR_SUCCESS;
}
static void toggleAutostart() {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &k,
                        nullptr) != ERROR_SUCCESS)
        return;
    if (autostart())
        RegDeleteValueW(k, L"WhatInput");
    else {
        auto v = L"\"" + (root / L"WhatInput.exe").wstring() + L"\"";
        RegSetValueExW(k, L"WhatInput", 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(v.c_str()),
                       DWORD((v.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(k);
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == taskbarMessage) {
        Shell_NotifyIconW(NIM_ADD, &tray);
        return 0;
    }
    switch (m) {
    case WM_INPUT:
        rawInput(reinterpret_cast<HRAWINPUT>(l));
        break;
    case WM_INPUT_DEVICE_CHANGE:
        if (w == GIDC_REMOVAL) {
            auto it = devices.find(reinterpret_cast<HANDLE>(l));
            if (it != devices.end()) {
                devices.erase(it);
            }
        } else {
            RID_DEVICE_INFO info{};
            info.cbSize = sizeof(info);
            UINT n = sizeof(info);
            if (GetRawInputDeviceInfoW(reinterpret_cast<HANDLE>(l),
                                       RIDI_DEVICEINFO, &info,
                                       &n) != UINT(-1) &&
                info.dwType != RIM_TYPEHID)
                device(reinterpret_cast<HANDLE>(l),
                       info.dwType == RIM_TYPEMOUSE);
        }
        break;
    case WM_POWERBROADCAST:
        if (w == PBT_APMSUSPEND) {
            sleeping = true;
            wake.notify_all();
        }
        if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) {
            ++generation;
            sleeping = false;
        }
        return TRUE;
    case WM_APP + 1:
        if (l == WM_LBUTTONDBLCLK)
            openStats();
        if (l == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"打开统计");
            AppendMenuW(menu, MF_STRING | (autostart() ? MF_CHECKED : 0), 2,
                        L"开机自启");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"退出");
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(h);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x,
                                     p.y, 0, h, nullptr);
            DestroyMenu(menu);
            if (cmd == 1)
                openStats();
            if (cmd == 2)
                toggleAutostart();
            if (cmd == 3)
                DestroyWindow(h);
        }
        return 0;
    case WM_APP + 2:
        openStats();
        return 0;
    case WM_APP + 3: {
        std::lock_guard lock(guard);
        clearRequested = true;
        wake.notify_all();
        return 1;
    }
    case WM_APP + 4: {
        std::lock_guard lock(guard);
        return lastError.empty() ? 1 : 2;
    }
    case WM_APP + 5: {
        std::lock_guard lock(guard);
        return LRESULT(lastCommit);
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        stopping = true;
        wake.notify_all();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    wchar_t exe[32768];
    GetModuleFileNameW(nullptr, exe, 32768);
    root = fs::path(exe).parent_path();
    if (command && std::wstring(command) == L"--exit") {
        if (auto h = FindWindowW(L"WhatInputCollector", nullptr))
            PostMessageW(h, WM_CLOSE, 0, 0);
        return 0;
    }
    HANDLE mutex =
        CreateMutexW(nullptr, FALSE, L"Local\\WhatInput.Collector.v1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (auto h = FindWindowW(L"WhatInputCollector", nullptr))
            PostMessageW(h, WM_APP + 2, 0, 0);
        CloseHandle(mutex);
        return 0;
    }
    try {
        fs::create_directories(root / L"data");
        fs::create_directories(root / L"logs");
        std::ofstream probe(root / L"data/.write-test");
        if (!probe)
            throw std::runtime_error("Run directory is not writable");
        probe.close();
        fs::remove(root / L"data/.write-test");
    } catch (...) {
        MessageBoxW(nullptr, L"程序目录不可写。请移动到可写目录后运行。",
                    L"WhatInput", MB_ICONERROR);
        return 1;
    }
    WNDCLASSW cls{};
    cls.lpfnWndProc = proc;
    cls.hInstance = instance;
    cls.lpszClassName = L"WhatInputCollector";
    RegisterClassW(&cls);
    window = CreateWindowW(cls.lpszClassName, L"WhatInput", 0, 0, 0, 0, 0,
                           nullptr, nullptr, instance, nullptr);
    if (!window)
        return 2;
    RAWINPUTDEVICE raw[] = {{1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window},
                            {1, 2, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window}};
    if (!RegisterRawInputDevices(raw, 2, sizeof(raw[0])))
        return 3;
    {
        UINT n = 0;
        GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST));
        std::vector<RAWINPUTDEVICELIST> list(n);
        if (GetRawInputDeviceList(list.data(), &n,
                                  sizeof(RAWINPUTDEVICELIST)) != UINT(-1))
            for (auto &d : list)
                if (d.dwType != RIM_TYPEHID)
                    device(d.hDevice, d.dwType == RIM_TYPEMOUSE);
    }
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = WM_APP + 1;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, L"WhatInput · 正在统计");
    Shell_NotifyIconW(NIM_ADD, &tray);
    taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    std::thread disk(writer), pads(gamepads);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    stopping = true;
    pads.join();
    diskStopping = true;
    wake.notify_all();
    disk.join();
    Shell_NotifyIconW(NIM_DELETE, &tray);
    CloseHandle(mutex);
    return 0;
}
