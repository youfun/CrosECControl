# CrosEC Control

<p align="center">
  <img src="assets/CrosECControl.png" alt="CrosEC Control icon" width="128">
</p>

<p align="center">
  <a href="README.md">中文</a> · <a href="README.en.md">English</a> · <a href="https://github.com/youfun/CrosECControl/releases/latest">Releases</a>
</p>

CrosEC Control is a lightweight native Win32 control panel. It uses native Win32 so it can run on low-memory machines, and it uses less memory than similar apps. It manages the ChromeOS Embedded Controller (EC) on Windows by turning common, relatively safe features of the locally installed `ectool` into buttons, sliders, and status panels, without typing commands in a terminal.

> This program is for devices that already have CoolStar’s CROS-EC Windows driver and `ectool`. Typical Windows PCs cannot use the hardware controls. This repository does not include `ectool`.

## Download

Get prebuilt binaries from [GitHub Releases](https://github.com/youfun/CrosECControl/releases/latest).

## Relationship to ectool

EC status, fans, keyboard backlight, charge limit, and read-only diagnostics are implemented by calling the locally installed `ectool.exe`. This repository does not include `ectool`.

## Features

- Locates `ectool.exe` automatically; if it is missing, shows a CoolStar crosec setup guide (GitHub links opened in the browser; no in-app download)
- Live EC connection status, peak temperature, fan speed, fan mode, and battery summary
- Live CPU, memory, and system-drive usage, with specs (CPU model such as i3-8100, logical processors, RAM size, disk model and capacity)
- Control / Diagnostics tabs so primary controls stay reachable on laptop-height screens
- Fan auto, off, max, and custom duty-cycle modes
- Keyboard backlight 0–100%
- Read-only diagnostics for battery, USB-C/PD, firmware, sensors, and more
- EC Battery Sustainer charge limit: 50–100%, with a 5% hysteresis window
- Third-party driver backup via Windows `pnputil`, plus a standalone restore script
- Stays in the system tray: minimize or close hides the window; double-click restores, right-click exits

## Requirements

### Runtime

- Windows 10/11 x64
- A compatible ChromeOS EC device
- CoolStar CROS-EC Windows driver
- `ectool.exe`

The program looks for `ectool.exe` in this order:

1. The directory that contains `CrosECControl.exe`
2. `C:\Program Files\crosec\ectool.exe`
3. `C:\Program Files (x86)\crosec\ectool.exe`
4. The system `PATH`

If the tool is missing or the EC cannot be reached, the program still starts and shows an error state in the UI.

If `ectool.exe` is not found at startup, a setup guide offers:

1. **GitHub direct link**: download the CoolStar `crosec` installer
2. **Directory page**: open the `coolstar/driverinstallers` crosec folder to pick a version

The program **does not** download or install the driver itself. Download the installer in the browser, finish setup, then click Refresh.

### Build

- Visual Studio 2022 Build Tools
- **Desktop development with C++** workload
- CMake 3.21 or later

## Build

In Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output:

```text
build\Release\CrosECControl.exe
```

The project uses C++20 and only Win32 libraries from the Windows SDK. No extra C++ package manager is required.

## Usage

### Status and diagnostics

The program refreshes EC status on startup, and you can click Refresh at any time. The overview always shows temperature, fans, mode, and a battery summary. Temperature and connection state use color.

The main window has two tabs:

- **Control**: fans, keyboard backlight, charge limit
- **Diagnostics**: read-only query buttons, output pane, driver backup, and diagnostic report export

Choosing a diagnostics item shows the full command output below; Copy Output copies the current text. Running a diagnostic switches to the Diagnostics tab automatically.

### System tray

After startup, an icon appears in the system tray:

- Minimize or close hides the main window; the program keeps running.
- Double-click the tray icon to restore the window.
- Right-click for Show main window or Exit.
- If Windows Explorer restarts, the program re-registers the tray icon.

To quit completely, use Exit on the tray menu. Closing the main window is not the same as quitting.

### Fan control

- **Auto**: return fan control to the EC.
- **Off**: set 0%. An overheating warning is shown first.
- **Max**: set 100%.
- **Custom**: set duty cycle from the slider.

If you Exit from the tray while the fan is in a manual mode, the program runs `ectool autofanctrl` to restore EC automatic control. Clicking Auto also returns control immediately. Hiding to the tray does not change the current fan mode.

### Keyboard backlight

Drag the slider to set keyboard backlight. The command runs asynchronously after a short debounce so dragging does not stall the UI. Devices without backlight control show `N/A` and disable the slider.

### Battery charge limit

This depends on EC firmware Battery Sustainer support:

- Limits are 50–100%.
- Applying a limit below 100% runs:

  ```text
  ectool chargecontrol normal <limit-5> <limit>
  ```

- Restore 100% runs:

  ```text
  ectool chargecontrol normal
  ```

For example, an 80% limit keeps charge around 75–80%. The program never uses `batterycutoff` for charge limiting.

### Back up Windows drivers

Click Back up drivers, choose a folder, and the program requests administrator rights. It then runs a read-only export with the built-in Windows command:

```text
pnputil /export-driver * <backup folder>\Drivers
```

Folder names look like:

```text
CrosEC-Driver-Backup-20260730-120000
```

The folder contains:

- `Drivers\`: third-party Driver Store packages exported by `pnputil`
- `Crosec-Tools\`: a copy of `C:\Program Files\crosec` if present
- `Backup.log`: full export log
- `README.txt`: backup notes
- `Restore-Drivers.cmd`: standalone restore script for use after reinstalling Windows

Inbox Windows drivers are not exported by `pnputil`; after reinstall they should come from Windows, Windows Update, or installation media.

## Safety

> **Warning: Incorrect hardware control can cause overheating, data loss, or an unstable device. Confirm compatibility and use at your own risk.**

- Turning the fan off can overheat the device quickly. The program asks for confirmation, but that does not replace temperature monitoring.
- The program does not expose firmware flashing, GPIO, I2C, EC reset, battery cutoff, or similar high-risk commands.
- Driver backup is an export only. It does not install, delete, or replace drivers.
- The main program has no driver restore button.
- Use `Restore-Drivers.cmd` only **on the same device after reinstalling Windows**. The script shows a risk warning and requires typing `RESTORE` before it continues.
- Do not run the restore script on a working system “just to test.” Incompatible drivers can break the device or prevent Windows from booting.

## Project layout

```text
CrosECControl/
├─ assets/                 # PNG and Windows ICO
├─ src/
│  ├─ main.cpp             # Win32 UI, ectool calls, and main features
│  ├─ app.manifest         # Privileges, compatibility, and DPI
│  ├─ resource.h           # Resource IDs
│  └─ resources.rc         # Windows resources
├─ tools/generate_icon.py  # Icon generator (requires Pillow)
├─ CMakeLists.txt
├─ AGENTS.md               # Conventions for coding agents
├─ README.md               # Chinese user guide
├─ README.en.md            # English user guide
└─ LICENSE
```

## Development and verification

There are no automated tests yet. After a change, run at least one Release build:

```powershell
cmake --build build --config Release
```

Hardware-related changes should be verified by hand on a compatible device. Coding conventions, threading, and safety limits are in [`AGENTS.md`](AGENTS.md).

To regenerate icons, install Pillow and run:

```powershell
py tools\generate_icon.py
```

## References and license

- **ectool / ChromiumOS EC**: BSD-3-Clause. This program calls the installed `ectool.exe` and does not embed its code.
- **Feature reference**: [Chrultrabook-Tools](https://github.com/death7654/Chrultrabook-Tools) (GPL-3.0)

This project is licensed under the [GNU General Public License v3.0](LICENSE). If you distribute the program (including the compiled exe), you must also provide the corresponding source and keep the GPL-3.0 terms.
