# 源码目录

| 目录 | 职责 |
| --- | --- |
| src/app | 程序入口、单实例与托盘生命周期、Windows 图标资源模板 |
| src/core | 输入计数模型、状态边沿、SQLite 存储、摇杆方向算法 |
| src/capture | Win32 Raw Input、XInput 采集、软件归属识别 |
| src/engine | 内存增量、存储工作线程、查询与结果模型 |
| src/platform | Windows 软件窗口标题与名称解析 |
| src/ui | 主窗口、菜单、时间和软件筛选、排行榜 |
| src/ui/keyboard | 键盘热力图 |
| src/ui/mouse | 鼠标统计页、热力图后台渲染 |
| src/ui/gamepad | 手柄热力图及方向扇区 |

跨模块头文件使用以 src 为根的路径，如 engine/engine.h。新增源文件同步登记到根目录 CMakeLists.txt，不依赖文件通配。

## 应用图标

- 唯一原始素材为 assets/ICON.png。
- packaging/make-icon.ps1 使用 Windows System.Drawing 生成 assets/WhatInput.ico，包含 16、24、32、48、64、128、256 像素图层，保留宽高比与透明背景。
- packaging/build.cmd 在 CMake 配置前自动生成图标；直接运行 CMake 时使用仓库内的 ICO，修改 PNG 后需先运行生成脚本。
- src/app/windows.rc.in 把图标嵌入 EXE，供资源管理器与原生窗口使用；Qt 资源也嵌入同一 ICO，供窗口、任务栏、托盘使用，无需运行时读取外部素材。
- 发布目录 assets 同时附带 PNG 与 ICO。其他软件的图标缓存继续显示对应软件的实际图标。

本次仅构建与部署，不执行测试；数据库格式未变，不清除历史记录。