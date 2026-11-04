# WhatInput

Windows 10/11 x64 本地输入统计工具，使用 C++20、Qt 6 Widgets、Win32 Raw Input/XInput 和 SQLite。

## 使用

启动工作区 `release/WhatInput.exe`，打开原生统计窗口并在托盘常驻。整个应用使用一个进程，内部线程负责输入采集、手柄轮询和存储。关闭统计窗口后继续记录，托盘“退出”才停止程序。

- 所有键盘合并、所有鼠标合并、所有 XInput 手柄合并，按设备类别区分键位。
- 按秒聚合，支持软件、设备类型、今天、昨天、最近 7 天、全部及自定义秒级区间。
- 窗口可见时约每 100 毫秒刷新变化，排行、软件排行、总次数包含尚未落盘的内存计数。
- 点击小时柱查看对应时段，点击“返回原时段”恢复。
- 默认每 **60 秒**保存；在“设置”中可修改为 **1～3600 秒**，设置会保存在程序目录。
- 界面和托盘提供“立即保存”；正常退出先停止采集，再保存剩余数据。
- CSV 导出包含选定范围内的数据库记录及未保存增量，写入 `exports/`。
- 清空历史需要二次确认，同时清空历史和清空操作之前尚未保存的计数。
- 开机自启使用当前用户权限，以 `--background` 模式启动，仅显示托盘图标。

界面底部显示待保存次数、最近落盘时间和存储错误。自动保存不会使界面计数再次增加。保存失败时保留内存批次重试；退出保存失败时可以重试，或明确选择丢弃未保存数据退出。

## 目录与数据

所有应用文件和缓存均放在 EXE 所在目录：

```text
release/
  WhatInput.exe
  Qt DLL、platforms/ 等运行依赖
  data/whatinput.sqlite3
  config/settings.json
  logs/whatinput.log
  exports/
  temp/
```

不使用 AppData，不保存输入文本或同秒内的事件顺序。鼠标新增按秒、屏幕、坐标、按键和软件聚合的位置次数，不记录移动轨迹或屏幕截图。软件显示名称实时读取当前窗口标题，无标题时显示进程名；不缓存或保存窗口标题，图标继续使用本地缓存。数据库永久保留，用户可以主动清空。关闭窗口不影响统计；隐藏/最小化时停止实时视图刷新。

异常崩溃、强制结束或断电可能丢失当前保存周期内的未提交数据，持续写入失败时可能更多。请将程序放在可写目录。

## 构建与发布

依赖：Qt **6.8.3 MSVC2022 x64 的 Widgets/Qt Base**、SQLite **3.50.4**、CMake ≥3.24、Ninja、兼容 MSVC 编译器及 Windows SDK。当前工作区使用 MSVC 19.51.36256、Windows SDK 10.0.26100.0。SQLite 源码已经固定在 `third_party/`。

```powershell
$env:VCVARS = '你的Visual Studio目录\VC\Auxiliary\Build\vcvars64.bat'
$env:QT_ROOT = '你的Qt目录\6.8.3\msvc2022_64'
cmd /c packaging\build.cmd
```

本机可直接运行脚本，默认路径已配置。脚本仅构建 `WhatInput`，先部署到 `build/package`，再更新工作区 `release`。不运行测试，也不启动新程序进行验证。更新时会正常退出工作区内的旧程序，替换应用和依赖，清除旧统计 EXE 及 WebEngine 运行组件。

本版存储版本为 **4**。根据本次用户明确要求，升级 v3 数据库时保留键盘和手柄记录，只在首次启动时通过事务删除旧鼠标计数并创建鼠标位置表，不删除整个数据库。再次启动不会重复清除数据；`release/.whatinput-schema-version` 记录发布版本。此次数据保留要求优先于工作区通常的整库重建规则。

请分发完整 `release` 目录。原生界面无需 HTML、JavaScript、WebChannel、QtWebEngineProcess 或 WebEngine 资源。首次在缺少 MSVC 运行库的机器使用时，安装配套 `vc_redist.x64.exe`。

## 边界与验证状态

### LOL 对局中不计数

2026-11-04 经用户授权进行本机训练营对照：LOL 为管理员权限、WhatInput 为普通权限时，对局中无法正常接收键鼠输入；以管理员权限启动 WhatInput 后，原有 Raw Input 采集恢复。请先从托盘退出已有实例，再右键 `WhatInput.exe` → “以管理员身份运行”。当前开机自启不会自动提权。该结果不代表所有游戏或反作弊系统的兼容保证。

详细证据见 [全屏输入诊断](docs/fullscreen-attribution.md)。`--capture-diagnostics` 为可选的 30 秒被动诊断模式，对比 Raw Input 与标准低级钩子的累计消息数量，输出到 `logs/capture-diagnostics.json`；结束后卸载诊断钩子，普通模式不安装钩子。

软件归属根据前台窗口或鼠标指针目标判断，不保证识别全局快捷键、鼠标捕获或后台手柄输入的实际消费者。浏览器不区分网站。Raw Input 的临时设备句柄和 XInput 插槽仅用于内存状态检测，落盘统一按三种类别累计。

Widgets 改造当时仅完成编译与部署；此后仅针对上述 LOL 故障进行了用户授权的实机诊断。旧版测试结果不代表整个线程架构已经通过验证。Windows 10、设备拔插、休眠唤醒、资源测量及长期运行仍需获得用户允许后验证。

设计见 `docs/dev.md`，构建记录见 `docs/validation.md`，第三方说明见 `THIRD_PARTY.md`。
