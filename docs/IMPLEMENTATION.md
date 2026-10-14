# 实现与验证边界

RebootGuard 是一个在用户登录会话中运行的 Win32 桌面程序。它组合了“取消待执行关机”和“否决正常会话结束”两种机制，尽力为后台任务争取继续运行的机会。

## 运行流程

1. 创建当前登录会话内的单实例互斥体。发现已有实例时，通知该实例显示菜单后退出。
2. 尝试启用当前进程的 `SE_SHUTDOWN_NAME` 权限，并设置关机通知顺序。失败时显示错误，退出启动流程。
3. 加载内嵌图标，创建隐藏的顶层窗口，添加托盘图标。
4. 注册关机阻止理由，启动约 1 秒一次的窗口定时器；成功后进入开启状态，并立即检查一次待执行关机。
5. 在消息循环中处理定时器、会话结束通知和托盘操作。

窗口采用隐藏顶层窗口，而非消息专用窗口，以接收相关会话广播。程序没有检查用户空闲时间或在锁屏时关闭保护的分支。

## 两种拦截机制

### 取消倒计时

`WM_TIMER` 驱动 `GuardState::Tick`，生产程序调用 `AbortSystemShutdownW(nullptr)`，尝试取消本机正在倒计时的关机请求。

| 调用结果 | 状态处理 |
| --- | --- |
| 成功 | 增加本次进程运行期间的取消计数，清除错误 |
| `ERROR_NO_SHUTDOWN_IN_PROGRESS`（1116） | 视为当前没有待取消关机，不显示错误 |
| 其他错误 | 保存错误码，显示橙色图标及菜单错误信息 |

该 API 的适用时机是关机倒计时尚未结束时。零或极短倒计时可能没有可用的取消窗口；窗口定时器还会受消息处理和系统调度影响，不能把 1 秒配置理解为严格的响应上限。[Microsoft：AbortSystemShutdownW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-abortsystemshutdownw)

### 否决正常会话结束

开启时通过 `ShutdownBlockReasonCreate` 注册可读理由；收到 `WM_QUERYENDSESSION` 时返回 `FALSE`，解除后返回 `TRUE`。普通关机、重启和注销都会经过相应会话结束流程，程序不按发起者或更新来源筛选。注册理由帮助 Windows 向用户说明阻止原因，不赋予程序不可被绕过的否决权。[Microsoft：WM_QUERYENDSESSION](https://learn.microsoft.com/en-us/windows/win32/shutdown/wm-queryendsession)、[ShutdownBlockReasonCreate](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-shutdownblockreasoncreate)

实际收到 `WM_ENDSESSION(TRUE)` 后，程序清理并退出；`WM_ENDSESSION(FALSE)` 表示本次结束会话未完成，程序继续工作。强制或关键关机可能绕过正常阻止流程。

## 状态与托盘

`GuardState` 保存是否开启、最近错误码和成功取消次数。解除时先清除开启标志，再停止定时器、注销阻止理由，保证已经排队的旧定时器消息不会重新执行取消操作。

三个内嵌图标分别表示：绿色暂停符号为开启，灰色播放符号为解除，橙色感叹号为取消异常。取消接口报错时，会话结束否决仍由开启标志控制；异常状态不能视作已解除。

托盘图标以窗口句柄和图标 ID 标识。收到 Explorer 的 `TaskbarCreated` 消息后尝试重新添加；图标已存在时改为更新。失败时使用独立的 2 秒定时器重试。退出时删除托盘图标并清理注册信息。消息过滤仅允许托盘恢复和显示菜单所需的自定义消息，不为低完整性调用方放行 `WM_COMMAND`。

## 锁屏、睡眠与更新策略

锁屏保留用户登录会话。只要电脑仍处于唤醒状态且进程正常运行，消息循环和保护逻辑就可继续执行；锁屏本身不是退出程序的条件。睡眠、休眠时程序不会持续执行，真正注销后进程也不再提供保护。[Microsoft：LockWorkStation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-lockworkstation)

程序不调用防睡眠接口，不修改电源计划，不关闭更新服务，也不写入 Windows 更新策略。更新下载、安装和重启策略需要独立配置，不能用本程序的运行状态推断这些策略已经生效。[Microsoft：Windows Update 设置](https://learn.microsoft.com/en-us/windows/deployment/update/waas-wu-settings)

## 构建、安装与发布

`build.ps1` 自动定位 Visual Studio C++ 工具链与 Windows SDK，生成 x64 单 EXE。图标经资源编译器嵌入，运行时不依赖旁置图标文件。

`install.ps1` 是可选的当前用户安装入口，优先使用发布包同目录的 EXE，否则使用 `build/` 中的 EXE。它复制文件至 `%LOCALAPPDATA%\RebootGuard`，比较源文件与安装文件的哈希，创建桌面和当前用户登录启动快捷方式，随后启动程序。已有 EXE 或任一同名快捷方式都会使安装拒绝继续。脚本支持 `-WhatIf`，不配置手动更新策略。便携使用只需运行 EXE；用户安装仍依赖登录会话，不是系统服务。

`package.ps1` 读取 `VERSION`，生成 `dist/RebootGuard-1.0.0-windows-x64.zip` 等对应版本的发布包，拒绝覆盖同版本已有产物。ZIP 保留顶层版本目录，内含 EXE、安装脚本、README、版本号、实现说明、图标预览及 EXE 的 SHA-256 清单；另生成 ZIP 的 SHA-256 清单。哈希用于检查文件完整性，不证明拦截效果。源码与测试保留在仓库中。

## 测试证明什么

| 验证层 | 覆盖范围 | 不覆盖的结论 |
| --- | --- | --- |
| 状态逻辑单元测试 | 开启/解除、模拟取消结果、计数、错误恢复、解除后的旧定时器事件 | Windows 是否实际取消一次真实关机 |
| 隔离原生生命周期测试 | 自有窗口的合成会话消息、阻止理由注册/撤销、托盘恢复/清理、进程退出 | 实际更新重启、强制关机、整夜稳定性 |

原生测试实例使用独立的窗口类名和互斥体，并以测试替身替换真实取消接口。测试先启动自己的进程，再核对窗口所属进程，仅向这个窗口发送合成消息，不向系统广播，也不发起关机或重启。关机阻止理由和托盘检查使用真实 Windows 接口，因此需要可用的交互桌面及 Explorer 托盘环境。

这些检查支持代码路径和资源生命周期的判断。它们不构成“所有重启都能阻止”的证明，也不构成已完成真实锁屏过夜测试的声明。断电、系统崩溃以及外部强制结束进程均不在程序能够保证保护的范围内。
