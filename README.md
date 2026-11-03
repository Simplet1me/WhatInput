# WhatInput

Windows 10/11 x64 本地输入统计工具。C++20、Win32 Raw Input/XInput、SQLite、Qt 6 WebEngine/WebChannel。

## 使用

发布目录为 `dist/WhatInput/`，启动 `WhatInput.exe` 后在系统托盘常驻，双击托盘打开统计，也可直接启动 `WhatInputStats.exe`。关闭统计窗口不停止采集；停止统计请选择托盘“退出”。菜单可配置当前用户开机自启，无暂停功能。

- 按秒聚合键盘、鼠标、手柄次数；长按不累计自动重复。
- 按软件、日期及秒级区间筛选；所有键盘合并、所有鼠标合并、所有手柄合并，不提供单设备筛选。
- 活动小时柱可下钻，按键排行包含次数及占比，另有软件排行。
- CSV 导出保存在 `exports/`；清空全部历史需要确认。
- 数据永久保留在 EXE 旁的 `data/whatinput.sqlite3`；缓存、日志、临时文件也在程序目录。不要放到不可写的 Program Files。
- 不记录输入文字、窗口标题、网址、鼠标轨迹或同秒内按键顺序。

首次运行后等待约 5 秒生成数据库。正常退出会提交剩余数据，强制结束或断电可能损失尚未提交的数据。备份/迁移时先正常退出两个程序，再复制整个目录。迁移后重新设置自启。

## 构建

已使用 Qt **6.8.3 MSVC2022 x64**、SQLite **3.50.4**、MSVC **19.51.36256**、Windows SDK **10.0.26100.0** 完成构建。Qt WebEngine 不支持 MinGW，不能用本机 MinGW 替代。

安装 Visual Studio C++ 工具链、Windows SDK、CMake ≥3.24、Ninja，以及 Qt 6.8.3 MSVC2022 x64 的 WebEngine、WebChannel、Positioning、SerialPort 模块。在 PowerShell 设置路径并运行：

```powershell
$env:VCVARS = '你的Visual Studio目录\VC\Auxiliary\Build\vcvars64.bat'
$env:QT_ROOT = '你的Qt目录\6.8.3\msvc2022_64'
cmd /c packaging\build.cmd
```

本工作区脚本默认使用已发现的本机工具链及 `.tools/Qt/6.8.3/msvc2022_64`。其他机器需确保 Ninja 在 PATH 中。脚本依次执行配置、Release 编译、CTest、安装及 windeployqt。单独构建采集器和测试可在 CMake 配置时传入 `-DBUILD_STATS=OFF`。

SQLite amalgamation 已随源码固定在 `third_party/`，无需在线下载。Qt 依赖下载器仅为开发工具，不是运行依赖。

## 部署

请分发完整 `dist/WhatInput` 目录，不要只复制 EXE。WebEngine 需要 `QtWebEngineProcess.exe`、`resources/`、`translations/qtwebengine_locales/`、平台插件和 Qt DLL。页面内嵌于 qrc，不依赖当前工作目录和网络。

Qt 6.8 的目标基线为 Windows 10 1809+ / Windows 11 x64；Windows 10 实机兼容性仍需复测。缺少 MSVC 运行库时安装发布目录内匹配的 `vc_redist.x64.exe`。正式分发还需遵守 `THIRD_PARTY.md` 中的许可证说明。

## 已知边界

- 软件归属代表输入时的前台/指针目标上下文，不能证明某个软件实际消费了全局快捷键或后台手柄输入。浏览器不细分网站。系统 Shell 分类包含桌面、任务栏、开始菜单等已识别窗口，文件资源管理器单列。
- 设备句柄和 XInput 插槽仅在内存中用于独立检测按下/释放和滚轮余量；落盘只区分键盘、鼠标、手柄。更换设备或插槽不会拆分历史统计。
- 安全桌面、特殊驱动、键盘启动/休眠期间未知按下状态、鼠标捕获和极短手柄点击需实机验证。轮询间隔不是捕获所有瞬时输入的保证。
- 旧版数据库首次由新版采集器打开时自动按类别合并，保留总次数、软件、秒和键位，并先在 `data/` 创建 `whatinput.sqlite3.pre-category-v1.bak` 备份。不要用旧版采集器继续写入升级后的数据库。

测试与实测记录见 [docs/validation.md](docs/validation.md)，需求设计见 [docs/dev.md](docs/dev.md)。
