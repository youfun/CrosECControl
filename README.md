# CrosEC Control

<p align="center">
  <img src="assets/CrosECControl.png" alt="CrosEC Control 图标" width="128">
</p>

<p align="center">
  <a href="README.md">中文</a> · <a href="README.en.md">English</a> · <a href="https://github.com/youfun/CrosECControl/releases/latest">Releases</a>
</p>

CrosEC Control 是一个轻量的原生 Win32 控制面板，为了能在低内存机器上使用，采用了 Win32 原生开发，同类软件中内存占用较低。它用于在 Windows 上管理 ChromeOS Embedded Controller（EC），把本机已安装的 `ectool` 中常用、相对安全的功能整理为按钮、滑块和状态面板，无需在终端中手动输入命令。

> 本程序面向安装了 CoolStar CROS-EC Windows 驱动及配套 `ectool` 的设备。普通 Windows 电脑通常无法使用其中的硬件控制功能。本仓库不包含 `ectool` 本身。

## 下载

预编译程序请到 [GitHub Releases](https://github.com/youfun/CrosECControl/releases/latest) 下载。

## 与 ectool 的关系

EC 状态、风扇、键盘背光、充电上限和只读诊断等功能，通过调用本机已安装的 `ectool.exe` 实现。本仓库不包含 `ectool`。

## 功能

- 自动查找 `ectool.exe`；未找到时提供 CoolStar crosec 安装引导（GitHub 直链，由浏览器打开，不内置下载）
- 实时显示 EC 连接状态、最高温度、风扇转速、风扇模式和电池摘要
- 实时显示 CPU、内存和系统盘占用率，并标注规格（CPU 型号如 i3-8100、逻辑线程数、内存容量、磁盘型号与容量）
- 控制 / 诊断分页，主控与只读工具分离，适配笔记本屏幕高度
- 风扇自动、关闭、最大和自定义占空比模式
- 0–100% 键盘背光调节
- 电池详情、USB-C/PD、固件、传感器等只读诊断
- EC Battery Sustainer 充电上限：50–100%，使用 5% 迟滞区间
- 通过 Windows `pnputil` 备份第三方驱动，并生成独立恢复脚本
- 系统托盘常驻：关闭或最小化时隐藏，支持双击恢复和右键退出

## 系统要求

### 运行

- Windows 10/11 x64
- 兼容的 ChromeOS EC 设备
- CoolStar CROS-EC Windows 驱动
- `ectool.exe`

程序会按以下顺序寻找 `ectool.exe`：

1. `CrosECControl.exe` 所在目录
2. `C:\Program Files\crosec\ectool.exe`
3. `C:\Program Files (x86)\crosec\ectool.exe`
4. 系统 `PATH`

找不到工具或无法连接 EC 时，程序仍会启动，并在界面中显示错误状态。

若启动时未找到 `ectool.exe`，会弹出安装引导，提供：

1. **GitHub 直链**：下载 CoolStar `crosec` 安装包
2. **目录页**：打开 `coolstar/driverinstallers` 的 crosec 目录，便于手动选择版本

程序本身**不会**自动下载或安装驱动；请在浏览器中下载安装包并完成安装，然后点击「立即刷新」。

### 编译

- Visual Studio 2022 Build Tools
- **Desktop development with C++** 工作负载
- CMake 3.21 或更高版本

## 编译

在 Developer PowerShell 中执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

生成文件：

```text
build\Release\CrosECControl.exe
```

项目使用 C++20，仅依赖 Windows SDK 中的 Win32 库，不需要额外 C++ 包管理器。

## 使用说明

### 状态与诊断

程序启动后会自动刷新 EC 状态，也可点击“立即刷新”。顶部概览固定显示温度、风扇、模式和电池摘要；温度与连接状态会用颜色区分。

主界面分为：

- **控制**：风扇、键盘背光、充电上限
- **诊断**：只读查询按钮、输出区、驱动备份与诊断报告导出

在诊断页选择项目后，完整命令输出显示在下方；点击“复制输出”可复制当前内容。执行诊断命令时会自动切换到诊断页。

### 系统托盘

程序启动后会在系统托盘显示图标：

- 点击窗口的最小化按钮或关闭按钮会隐藏主窗口，程序继续在后台运行。
- 双击托盘图标可恢复主窗口。
- 右键托盘图标可选择“显示主窗口”或“退出”。
- Windows 资源管理器重启后，程序会自动重新注册托盘图标。

需要完全结束程序时，请使用托盘菜单中的“退出”；关闭主窗口不等同于退出。

### 风扇控制

- **自动**：把风扇控制权交还给 EC。
- **关闭**：设置为 0%，执行前会显示过热警告。
- **最大**：设置为 100%。
- **自定义**：按滑块百分比设置风扇占空比。

如果通过托盘菜单退出时程序仍处于手动风扇模式，会执行 `ectool autofanctrl`，尽量恢复 EC 自动风扇控制；点击“自动”也会立即交还控制权。隐藏到托盘不会改变当前风扇模式。

### 键盘背光

拖动滑块即可设置键盘背光。命令会在短暂防抖后异步执行，避免拖动过程中连续阻塞界面。不支持键盘背光控制的设备会显示 `N/A` 并禁用滑块。

### 电池充电上限

该功能依赖 EC 固件的 Battery Sustainer 支持：

- 可选上限为 50–100%。
- 应用低于 100% 的上限时，程序执行：

  ```text
  ectool chargecontrol normal <上限-5> <上限>
  ```

- 点击“恢复 100%”时执行：

  ```text
  ectool chargecontrol normal
  ```

例如上限设为 80% 时，EC 会把电量维持在约 75%～80%。程序不会使用 `batterycutoff` 实现充电限制。

### 备份 Windows 驱动

点击“备份驱动”并选择保存位置后，程序会请求管理员权限，并使用 Windows 自带命令执行只读导出：

```text
pnputil /export-driver * <备份目录>\Drivers
```

生成的目录名类似：

```text
CrosEC-Driver-Backup-20260730-120000
```

目录包含：

- `Drivers\`：`pnputil` 导出的第三方 Driver Store 驱动包
- `Crosec-Tools\`：`C:\Program Files\crosec` 的副本（若存在）
- `Backup.log`：完整导出日志
- `README.txt`：备份说明
- `Restore-Drivers.cmd`：重装系统后使用的独立恢复脚本

Windows 内置驱动不会被 `pnputil` 导出，重装后应由 Windows、Windows Update 或安装介质提供。

## 安全说明

> **警告：错误的硬件控制可能导致过热、数据丢失或设备不稳定。请确认设备兼容，并自行承担操作风险。**

- 关闭风扇可能导致设备迅速过热。程序会要求二次确认，但不能替代温度监控。
- 本程序不公开固件刷写、GPIO、I2C、EC 重置、电池断电等高风险命令。
- 驱动备份是导出操作，不会安装、删除或替换当前驱动。
- 主程序没有驱动恢复按钮。
- `Restore-Drivers.cmd` 仅应用于**同一台设备重装 Windows 之后**。脚本会显示风险警告并要求输入 `RESTORE` 后才继续。
- 不要为了测试备份而在正常工作的系统中运行恢复脚本；不兼容驱动可能导致设备故障或 Windows 无法正常启动。

## 项目结构

```text
CrosECControl/
├─ assets/                 # PNG 和 Windows ICO 图标
├─ src/
│  ├─ main.cpp             # Win32 UI、ectool 调用和主要功能
│  ├─ app.manifest         # 权限、兼容性和 DPI 配置
│  ├─ resource.h           # 资源 ID
│  └─ resources.rc         # Windows 资源定义
├─ tools/generate_icon.py  # 图标生成脚本（需要 Pillow）
├─ CMakeLists.txt
├─ AGENTS.md               # 编码代理开发约定
├─ README.md               # 中文说明
├─ README.en.md            # English guide
└─ LICENSE
```

## 开发与验证

项目目前没有自动化测试。修改后至少执行一次 Release 构建：

```powershell
cmake --build build --config Release
```

涉及硬件行为的修改应在兼容设备上人工验证。详细代码约定、线程模型和安全边界见 [`AGENTS.md`](AGENTS.md)。

如需重新生成图标，请先安装 Pillow，再运行：

```powershell
py tools\generate_icon.py
```

## 参考与许可

- **ectool / ChromiumOS EC**：BSD-3-Clause。本程序调用已安装的 `ectool.exe`，不内嵌其代码。
- **功能参考**：[Chrultrabook-Tools](https://github.com/death7654/Chrultrabook-Tools)（GPL-3.0）

本项目采用 [GNU General Public License v3.0](LICENSE)。分发本程序（包括编译出的 exe）时，必须同时提供对应源码，并保持 GPL-3.0 条款。
