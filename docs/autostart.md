# 管理员权限与登录自启

## 手动启动

WhatInput 的最终 EXE 由 MSVC 链接器嵌入 `requireAdministrator`、`uiAccess=false` 清单。Windows 在进入程序前处理 UAC；拒绝授权时不会启动采集、创建数据或修改自启。无需在兼容性属性中勾选“以管理员身份运行”，已有该标记也不会影响内嵌清单。

程序通过 TokenElevation 确认实际权限。当前版本要求本机管理员账户本人登录，进程 SID 必须与当前 Windows 会话用户 SID 一致；标准用户输入另一个管理员账户凭据的跨账户运行会被明确拒绝。

## 设置开关

设置弹窗直接读取当前用户的真实任务：不存在或禁用显示关闭，定义匹配显示开启，路径或定义不匹配显示需要修复，权限不足或读取失败显示状态未知。未知状态不会被当成关闭，也不会在只保存自动保存间隔时修改任务。

勾选后保存会创建或更新任务，再读取校验；失败时保留设置弹窗并显示具体操作与系统错误码。未完成的更新尝试恢复原有的本软件任务定义。关闭自启仅删除任务，不结束当前统计进程。

## 任务定义

- 名称为 `WhatInput-<当前用户 SID>`，Source 为 `WhatInput.LoginAutostart.v1`。
- Principal 与 LogonTrigger 绑定同一个用户 SID，使用 InteractiveToken、HighestAvailable，不保存密码，不使用 SYSTEM 或 S4U。
- 动作路径与工作目录使用规范绝对路径，参数为 `--autostart --startup-hidden`。前者记录登录启动来源，后者隐藏主窗口并保留托盘；旧 `--background` 参数仍可手动使用。
- IgnoreNew、ExecutionTimeLimit=PT0S；允许电池供电，切换电池不停止；不依赖网络，不允许任务强制结束程序，不补跑错过的登录触发。
- 原生 Task Scheduler COM 接口在正确初始化的线程中执行，进程 COM 安全在 Qt 初始化前设置；不使用外部临时 XML 文件。
- 只更新或删除带本软件来源标记的任务，或当前路径和用户严格匹配的旧 WhatInput 任务。其他同名任务会报告冲突，不覆盖。
- 成功更新后仅清理路径与当前程序严格匹配的旧 Run 自启项。移动程序后需在新位置重新保存自启；新来源标记允许识别并修复属于本软件的旧路径任务。

注册时仅确认程序文件和工作目录有效，不检查 EXE、DLL 或安装目录的访问权限，不因目录可写而拒绝自启，也不更改目录 ACL 或系统 UAC 策略。

## 移除与诊断

卸载或删除程序前，应在设置中关闭自启；也可运行 `WhatInput.exe --disable-autostart` 清理本软件当前用户任务。`--enable-autostart` 可由部署工具显式配置，但不会触发任务或开始采集。

`logs/startup.log` 记录启动来源、已提升状态及用户 SID；`logs/autostart.log` 记录任务 API 错误；配置命令失败时输出 `logs/autostart-setup.log`。日志均保存在程序目录。

注册任务不会启动它，也不会运行登录验收。真实登录、UAC 拒绝、电池、重复启动和跨账户边界仍须按 AGENTS.md 单独获得本次测试许可。
