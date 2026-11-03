# 第三方依赖

## SQLite 3.50.4

来源：https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip

下载压缩包 SHA-256：`1D3049DD0F830A025A53105FC79FD2AB9431AEA99E137809D064D8EE8356B032`。

SQLite 属于 public domain；声明保留于 `third_party/sqlite3.c` 和 `sqlite3.h` 文件头。官方说明：https://www.sqlite.org/copyright.html

## Qt 6.8.3

通过 aqtinstall 3.3.0 从 Qt 官方镜像下载安装 MSVC2022 x64 二进制包；运行时动态链接。当前应用使用 Qt Widgets/Qt Base，运行包也可能包含部署工具选择的 Qt 插件。Qt 模块涉及 LGPL/GPL/商业许可。正式分发必须保留对应许可证和第三方通知，满足适用 LGPL 的动态替换、源码获取及其他要求，或使用适当商业许可。

Qt 源码：https://download.qt.io/archive/qt/6.8/6.8.3/single/

许可证参考：https://doc.qt.io/qt-6.8/licensing.html

当前版本不再使用或分发 Qt WebEngine/Chromium 组件。

## Microsoft 运行库

通过匹配工具链的 windeployqt 部署 VC++ Redistributable。分发以 Microsoft 对应许可条款为准。
