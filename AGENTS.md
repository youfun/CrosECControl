# AGENTS.md

本文档适用于仓库根目录及其所有子目录，供在本项目中工作的编码代理使用。

## 项目概述

CrosEC Control 是一个仅面向 Windows 10/11 的原生 Win32 桌面程序。它将 CoolStar CROS-EC 工具中的常用 `ectool` 命令封装为图形界面，用于查看 EC 状态，以及控制风扇、键盘背光和电池充电上限。

项目刻意保持轻量：没有第三方 C++ 依赖、包管理器或 UI 框架，主要逻辑集中在 `src/main.cpp`。

## 目录结构

- `CMakeLists.txt`：CMake 构建定义、C++ 标准及 Windows 链接库。
- `src/main.cpp`：窗口、控件、异步任务、`ectool` 调用、解析和驱动备份逻辑。
- `src/app.manifest`：权限、系统兼容性和 Per-Monitor V2 DPI 配置。
- `src/resource.h`、`src/resources.rc`：Windows 资源 ID 和程序图标资源。
- `assets/CrosECControl.ico`：编译进可执行文件的图标。
- `assets/CrosECControl.png`：图标的高分辨率 PNG 版本。
- `tools/generate_icon.py`：使用 Pillow 重新生成 PNG 和 ICO；除非明确修改图标，否则不要运行。
- `build/`：本机构建目录，已被 Git 忽略，不应提交。

## 构建环境与命令

要求：

- Windows 10/11 x64
- Visual Studio 2022 Build Tools，并安装 **Desktop development with C++**
- CMake 3.21 或更高版本

在 Developer PowerShell 中配置并构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

产物位于：

```text
build\Release\CrosECControl.exe
```

只需验证已有构建目录时，可以运行：

```powershell
cmake --build build --config Release
```

项目目前没有自动化测试。每次代码修改至少必须完成 Release 构建；涉及界面、硬件命令或驱动备份的修改，还需要按下文进行人工验证。

## 实现约定

### C++ 与 Win32

- 使用 C++20，保持 MSVC `/W4 /utf-8 /permissive-` 下无新增警告。
- 保持 Unicode Win32 API：优先使用宽字符类型和 `...W` 函数。
- 用户可见文本当前使用简体中文；代码标识符和技术注释使用英文。
- 延续现有直接、轻量的 Win32 风格，不要为了小功能引入 UI 框架或第三方运行时。
- 新增控件时，应在 `ControlId` 中分配 ID，在 `createUi()` 中创建，在 `WM_COMMAND` 或对应消息中处理，并检查 `layout()` 是否需要同步调整。
- Windows 资源 ID 必须集中维护在 `src/resource.h`，资源声明放在 `src/resources.rc`。
- 保持托盘生命周期：最小化和窗口关闭只隐藏窗口；仅托盘菜单“退出”真正销毁窗口；资源管理器重启后通过 `TaskbarCreated` 重新注册图标。

### 线程与消息

- 只能在 UI 线程直接读写窗口和控件状态。
- 慢速 `ectool`、文件复制和驱动导出操作不得阻塞 UI 线程。
- 后台线程通过 `PostMessageW` 把结果交回窗口过程；堆上消息数据的所有权必须清晰：发送失败时由发送方释放，发送成功后由消息处理方释放。
- 所有 `ectool` 进程调用继续通过 `gProcessMutex` 串行化，避免多个 EC 命令并发执行。
- 退出时必须可靠停止并 `join` 可连接的工作线程；不要让后台线程访问已销毁的窗口。

### `ectool` 集成

- 保留 `ectool.exe` 的查找顺序：程序目录、`C:\Program Files\crosec`、`C:\Program Files (x86)\crosec`、系统 `PATH`。
- 使用参数数组调用 `runEctool()`，不要拼接未经转义的用户输入。
- 不得增加任意命令输入框或原始命令执行入口。
- 子进程应继续隐藏运行，标准输出与错误输出应被捕获并显示在诊断区。
- 解析器必须容忍命令失败、空输出和不支持的固件功能；不要因为某个 EC 输出格式缺失而崩溃。

## 安全边界

这是硬件控制程序。以下规则属于项目约束，不得为了便利绕过：

- “关闭风扇”必须保留明确警告和二次确认。
- 程序在使用手动风扇模式后真正退出时，必须调用 `ectool autofanctrl` 恢复 EC 自动控制。
- 不公开刷写固件、GPIO、I2C、EC 重置、电池断电或类似高风险操作。
- 恢复 100% 充电只能使用 `ectool chargecontrol normal`，绝不能用 `batterycutoff`。
- 充电上限只在固件支持 Battery Sustainer 时启用，并继续使用 5% 的上下限迟滞区间。
- 驱动备份只能导出，不得在主程序中安装、删除或替换驱动。
- `Restore-Drivers.cmd` 必须继续显示风险提示、要求输入 `RESTORE`，并仅作为重装 Windows 后的独立恢复工具。
- 不要在开发机上为“测试”实际关闭风扇、切断电池、安装驱动或执行恢复脚本。

## 修改与验证流程

1. 修改前运行 `git status --short`，保留并理解用户已有的未提交改动，不要覆盖无关修改。
2. 阅读相关实现；当前多数功能都在 `src/main.cpp`，避免仅凭 README 推断行为。
3. 进行最小范围修改，并同步更新用户可见文档。
4. 运行 Release 构建并处理所有编译错误和新增警告。
5. 在可用的 CROS-EC 设备上人工验证相关功能；没有对应硬件时，必须明确说明未验证范围。

人工检查建议：

- 无 `ectool.exe` 时程序仍能启动并显示“未找到”。
- EC 状态刷新不会卡住窗口，关闭窗口时进程能干净退出。
- 风扇、背光和充电命令的按钮、确认框及状态更新符合预期。
- 手动风扇模式退出时恢复自动控制。
- 诊断输出保留换行和横向滚动，复制功能正常。
- 驱动备份路径、UAC 取消、成功和失败分支均能给出清晰结果；不要实际运行生成的恢复脚本。
- 最小化和关闭按钮会隐藏到托盘；双击可恢复，右键菜单可显示或退出。
- Windows 资源管理器重启后托盘图标能够恢复，退出后不遗留图标。
- DPI 缩放、窗口调整以及图标显示没有明显回归。

## 文档要求

如果功能、系统要求、命令、安全行为或输出路径发生变化，应在同一修改中更新 `README.md` 与 `README.en.md`。README 面向最终用户（中英对照），本文档面向编码代理；描述必须与当前源码一致。
