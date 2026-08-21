#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <winioctl.h>
#include "resource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
constexpr wchar_t kClassName[] = L"CrosECControl.Native.Window";
constexpr UINT WM_EC_UPDATE = WM_APP + 1;
constexpr UINT WM_EC_RESULT = WM_APP + 2;
constexpr UINT WM_DRIVER_BACKUP = WM_APP + 3;
constexpr UINT WM_TRAY_ICON = WM_APP + 4;
constexpr UINT WM_DIAGNOSTIC_REPORT = WM_APP + 5;
constexpr UINT WM_ECTOOL_GUIDE = WM_APP + 6;
constexpr UINT_PTR TIMER_SLIDER = 11;
constexpr UINT_PTR TIMER_SYSTEM = 12;
constexpr UINT kTrayIconId = 1;

// CoolStar crosec installer (includes ectool.exe). Links only — no in-app download.
constexpr wchar_t kCrosecInstallerName[] = L"crosec.2.0.7-installer.exe";
constexpr wchar_t kCrosecGithubDirect[] =
    L"https://github.com/coolstar/driverinstallers/raw/master/crosec/crosec.2.0.7-installer.exe";
constexpr wchar_t kCrosecGithubBrowse[] =
    L"https://github.com/coolstar/driverinstallers/tree/master/crosec";

constexpr COLORREF kBg = RGB(18, 20, 23);
constexpr COLORREF kPanel = RGB(28, 31, 36);
constexpr COLORREF kPanel2 = RGB(36, 40, 46);
constexpr COLORREF kBorder = RGB(48, 54, 62);
constexpr COLORREF kText = RGB(240, 243, 247);
constexpr COLORREF kMuted = RGB(157, 164, 175);
constexpr COLORREF kAccent = RGB(42, 122, 245);
constexpr COLORREF kGreen = RGB(55, 190, 125);
constexpr COLORREF kWarn = RGB(230, 170, 60);
constexpr COLORREF kDanger = RGB(235, 90, 90);
constexpr int kPad = 20;
constexpr int kMinClientW = 760;
constexpr int kMinClientH = 640;
constexpr int kHeaderH = 48;
constexpr int kOverviewH = 210;
constexpr int kTabHeaderH = 30;

struct Result { DWORD exitCode = 1; std::wstring text; };
struct Snapshot {
    bool connected = false;
    std::wstring status, temps, fans, keyboard, charge, battery;
    std::wstring batteryState;
    int maxTemp = -1, keyboardPercent = -1;
    int chargeLower = -1, chargeUpper = -1;
    int designCapacity = -1, fullChargeCapacity = -1, remainingCapacity = -1;
    int batteryVoltageMv = -1, batteryCurrentMa = 0, cycleCount = -1;
    int batteryPercent = -1, batteryHealth = -1, estimatedMinutes = -1;
    double batteryPowerWatts = 0.0;
    bool chargeSupported = false, sustainerEnabled = false;
    std::vector<int> rpms;
};
struct SystemMetrics {
    int cpuPercent = -1;
    int memoryPercent = -1;
    int diskPercent = -1;
    int cpuLogicalCount = 0;
    ULONGLONG memoryTotalBytes = 0;
    ULONGLONG diskTotalBytes = 0;
    wchar_t diskLetter = L'C';
    std::wstring cpuModel;
    std::wstring diskModel;
};
struct AsyncResult { std::wstring title, text; bool error = false; };
struct BackupResult { std::wstring folder, message; bool success = false; };
struct ReportResult { std::wstring path, message; bool success = false; };
struct DiagnosticCommand { std::wstring title; std::vector<std::wstring> args; };

enum ControlId {
    ID_STATUS = 100, ID_TEMP, ID_FANS, ID_MODE,
    ID_AUTO, ID_OFF, ID_MAX, ID_CUSTOM, ID_FAN_SLIDER, ID_FAN_VALUE,
    ID_KB_SLIDER, ID_KB_VALUE,
    ID_CHARGE_SLIDER, ID_CHARGE_VALUE, ID_CHARGE_STATUS, ID_CHARGE_APPLY, ID_CHARGE_RESET,
    ID_REFRESH, ID_BATTERY, ID_VERSION, ID_CHIP, ID_PROTOCOL, ID_SENSORS,
    ID_USB_C, ID_EC_STATUS, ID_DEVICE_STATUS, ID_FIRMWARE_INFO, ID_DIAGNOSTIC_REPORT,
    ID_DRIVER_BACKUP, ID_DIAG_TITLE, ID_OUTPUT, ID_COPY, ID_PATH,
    ID_TAB_CONTROL, ID_TAB_DIAG, ID_HEADER_SUB, ID_OVERVIEW_TITLE,
    ID_TEMP_CAPTION, ID_FANS_CAPTION, ID_MODE_CAPTION,
    ID_CPU, ID_MEM, ID_DISK, ID_CPU_CAPTION, ID_MEM_CAPTION, ID_DISK_CAPTION,
    ID_FAN_TITLE, ID_KB_TITLE, ID_CHARGE_TITLE, ID_DIAG_SECTION,
    ID_TRAY_SHOW, ID_TRAY_EXIT
};

enum class UiSurface { Background, Panel, Muted, AccentValue };

struct UiItem {
    HWND hwnd = nullptr;
    UiSurface surface = UiSurface::Background;
};

HWND gWindow{}, gStatus{}, gTemp{}, gFans{}, gMode{}, gFanSlider{}, gFanValue{};
HWND gKbSlider{}, gKbValue{}, gChargeSlider{}, gChargeValue{}, gChargeStatus{};
HWND gBatteryHealth{}, gBatteryPower{}, gBatteryTime{};
HWND gOutput{}, gDiagTitle{}, gPath{}, gBackupButton{}, gReportButton{};
HWND gTabControl{}, gTabDiag{}, gHeaderSub{}, gRefreshBtn{}, gOverviewTitle{};
HWND gTempCaption{}, gFansCaption{}, gModeCaption{};
HWND gCpu{}, gMem{}, gDisk{}, gCpuCaption{}, gMemCaption{}, gDiskCaption{};
HWND gFanTitle{}, gKbTitle{}, gChargeTitle{}, gDiagSection{};
HWND gChargeApply{}, gChargeReset{};
HWND gBtnAuto{}, gBtnOff{}, gBtnMax{}, gBtnCustom{};
HWND gBtnBattery{}, gBtnUsb{}, gBtnEcStatus{}, gBtnDevice{}, gBtnFirmware{};
HWND gBtnSensors{}, gBtnProtocol{}, gBtnCopy{};
HFONT gFont{}, gFontSmall{}, gFontLarge{}, gFontTitle{}, gFontMono{}, gFontStatus{};
HBRUSH gBgBrush{}, gPanelBrush{}, gPanel2Brush{}, gEditBrush{};
HBRUSH gStatusOkBrush{}, gStatusBadBrush{}, gStatusWaitBrush{};
std::vector<UiItem> gUiItems;
std::vector<HWND> gControlPage;
std::vector<HWND> gDiagPage;
RECT gCardTemp{}, gCardFans{}, gCardMode{}, gCardCpu{}, gCardMem{}, gCardDisk{};
RECT gOverviewPanel{}, gControlPanel{}, gDiagPanel{}, gStatusBar{};
std::filesystem::path gEctool;
std::atomic_bool gStop{false}, gRefreshNow{false};
std::thread gWorker;
std::mutex gProcessMutex;
NOTIFYICONDATAW gTrayIcon{};
UINT gTaskbarCreatedMessage = 0;
bool gManualFan = false;
bool gChargeEditing = false;
bool gTrayIconAdded = false;
bool gTrayHintShown = false;
bool gExitRequested = false;
bool gHaveSnapshot = false;
bool gLastConnected = false;
bool gDisconnectNotified = false;
int gTemperatureAlertLevel = 0;
bool gFanAlertActive = false;
int gPendingSlider = 0; // 1 fan, 2 keyboard; charge is only applied with its button
int gActiveTab = 0; // 0 control, 1 diagnostics
int gLastMaxTemp = -1;
bool gLastEcConnected = false;
COLORREF gTempColor = kText;
COLORREF gStatusColor = kMuted;
COLORREF gCpuColor = kText;
COLORREF gMemColor = kText;
COLORREF gDiskColor = kText;
std::vector<int> gLastRpms;
FILETIME gPrevIdle{}, gPrevKernel{}, gPrevUser{};
bool gHaveCpuSample = false;

void setText(HWND hwnd, const std::wstring& text);
void setOutput(const std::wstring& title, const std::wstring& text);
bool addTrayIcon(HWND hwnd);
void updateTrayFromSnapshot(const Snapshot& snapshot);
void layout(HWND hwnd);
void applyTabVisibility();
void paintUi(HWND hwnd, HDC dc);
void showEctoolInstallGuide(HWND owner);

std::wstring utf8ToWide(const std::string& input) {
    if (input.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    if (!n) { cp = CP_ACP; n = MultiByteToWideChar(cp, 0, input.data(), static_cast<int>(input.size()), nullptr, 0); }
    std::wstring decoded(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(cp, 0, input.data(), static_cast<int>(input.size()), decoded.data(), n);

    // A native multiline EDIT expects CRLF. ectool generally emits LF, while
    // some builds emit CRLF; normalize both without collapsing records.
    std::wstring out;
    out.reserve(decoded.size() + 32);
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i] == L'\r') {
            if (i + 1 < decoded.size() && decoded[i + 1] == L'\n') ++i;
            out += L"\r\n";
        } else if (decoded[i] == L'\n') {
            out += L"\r\n";
        } else {
            out += decoded[i];
        }
    }
    return out;
}

std::filesystem::path locateEctool() {
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(module).parent_path() / L"ectool.exe",
        L"C:\\Program Files\\crosec\\ectool.exe",
        L"C:\\Program Files (x86)\\crosec\\ectool.exe"
    };
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"ectool.exe", nullptr, MAX_PATH, found, nullptr)) candidates.emplace_back(found);
    for (const auto& path : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) return path;
    }
    return {};
}

void updateEctoolPathLabel() {
    if (!gPath) return;
    if (gEctool.empty())
        setText(gPath, L"ECTOOL：未找到 · 点击「立即刷新」可打开安装引导");
    else
        setText(gPath, L"ECTOOL：" + gEctool.wstring());
}

bool refreshEctoolLocation() {
    gEctool = locateEctool();
    updateEctoolPathLabel();
    return !gEctool.empty();
}

void openBrowserUrl(HWND owner, const wchar_t* url) {
    if (!url || !url[0]) return;
    const HINSTANCE result = ShellExecuteW(owner, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(owner,
                    (std::wstring(L"无法打开浏览器，请手动复制链接：\n\n") + url).c_str(),
                    L"打开链接失败",
                    MB_ICONWARNING | MB_OK);
    }
}

// Offer GitHub links only. User downloads and installs themselves.
void showEctoolInstallGuide(HWND owner) {
    enum : int {
        IdGithub = 1001,
        IdBrowse,
        IdClose = IDCANCEL
    };

    const TASKDIALOG_BUTTON buttons[] = {
        {IdGithub, L"GitHub 直链下载安装包"},
        {IdBrowse, L"打开 crosec 目录页\n浏览全部版本与说明"},
        {IdClose, L"稍后手动安装"},
    };

    for (;;) {
        std::wstring content =
            L"本程序需要 CoolStar CROS-EC 工具包中的 ectool.exe。\n"
            L"请下载并运行安装包（会安装驱动与 ectool 到 C:\\Program Files\\crosec）。\n\n"
            L"安装包：";
        content += kCrosecInstallerName;
        content +=
            L"\n\n"
            L"说明：\n"
            L"• 本程序不内置下载，只打开浏览器链接。\n"
            L"• 安装完成后回到本程序点击「立即刷新」。";

        TASKDIALOGCONFIG config{};
        config.cbSize = sizeof(config);
        config.hwndParent = owner;
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        config.pszWindowTitle = L"安装 CROS-EC 工具";
        config.pszMainInstruction = L"未找到 ectool.exe";
        config.pszContent = content.c_str();
        config.pButtons = buttons;
        config.cButtons = static_cast<UINT>(std::size(buttons));
        config.nDefaultButton = IdGithub;
        config.pszMainIcon = TD_INFORMATION_ICON;

        int clicked = 0;
        const HRESULT hr = TaskDialogIndirect(&config, &clicked, nullptr, nullptr);
        if (FAILED(hr)) {
            const int choice = MessageBoxW(
                owner,
                L"未找到 ectool.exe。\n\n"
                L"需要安装 CoolStar crosec 工具包。\n"
                L"是否打开 GitHub 直链下载页？",
                L"安装 CROS-EC 工具",
                MB_ICONINFORMATION | MB_YESNOCANCEL | MB_DEFBUTTON1);
            if (choice == IDYES) openBrowserUrl(owner, kCrosecGithubDirect);
            else if (choice == IDNO) openBrowserUrl(owner, kCrosecGithubBrowse);
            break;
        }

        if (clicked == IdGithub) {
            openBrowserUrl(owner, kCrosecGithubDirect);
        } else if (clicked == IdBrowse) {
            openBrowserUrl(owner, kCrosecGithubBrowse);
        } else {
            break;
        }

        const int again = MessageBoxW(
            owner,
            L"已在浏览器中打开链接。\n\n"
            L"若下载失败，可继续选择其他链接。\n"
            L"安装完成后请点击「立即刷新」。\n\n"
            L"是否继续选择其他下载方式？",
            L"下载引导",
            MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON2);
        if (again != IDYES) break;
    }
}

std::wstring quoteArg(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += c; slashes = 0; continue; }
        out.append(slashes, L'\\'); slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); out += L'\"';
    return out;
}

Result runEctool(const std::vector<std::wstring>& args) {
    std::lock_guard lock(gProcessMutex);
    if (gEctool.empty()) return {ERROR_FILE_NOT_FOUND, L"找不到 ectool.exe。请安装 CROS-EC 驱动工具。"};

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe{}, writePipe{};
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return {GetLastError(), L"无法创建输出管道。"};
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = quoteArg(gEctool.wstring());
    for (const auto& arg : args) command += L" " + quoteArg(arg);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(gEctool.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, gEctool.parent_path().c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!ok) { DWORD err = GetLastError(); CloseHandle(readPipe); return {err, L"无法启动 ectool.exe（错误 " + std::to_wstring(err) + L"）。"}; }

    std::string bytes;
    char buffer[4096]; DWORD got{};
    while (ReadFile(readPipe, buffer, sizeof(buffer), &got, nullptr) && got) bytes.append(buffer, got);
    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return {code, utf8ToWide(bytes)};
}

int parseMaxTemp(const std::wstring& text) {
    std::wregex pattern(LR"(=\s*(-?\d+)\s*C\))");
    int result = -1;
    for (std::wsregex_iterator i(text.begin(), text.end(), pattern), end; i != end; ++i)
        result = std::max(result, std::stoi((*i)[1].str()));
    return result;
}
std::vector<int> parseRpms(const std::wstring& text) {
    std::vector<int> values;
    std::wregex pattern(LR"(Fan\s+\d+\s+RPM:\s*(\d+))", std::regex::icase);
    for (std::wsregex_iterator i(text.begin(), text.end(), pattern), end; i != end; ++i)
        values.push_back(std::stoi((*i)[1].str()));
    return values;
}
int parsePercent(const std::wstring& text) {
    std::wregex pattern(LR"((\d+)\s*%?)"); std::wsmatch m;
    if (std::regex_search(text, m, pattern)) return std::clamp(std::stoi(m[1].str()), 0, 100);
    return -1;
}

int parseIntegerField(const std::wstring& text, const wchar_t* label) {
    std::wregex pattern(std::wstring(label) + LR"(\s*:?[ \t]*(-?\d+))", std::regex::icase);
    std::wsmatch match;
    return std::regex_search(text, match, pattern) ? std::stoi(match[1].str()) : -1;
}

void parseBattery(Snapshot& snapshot, const Result& result) {
    snapshot.battery = result.text;
    if (result.exitCode != 0) return;

    snapshot.designCapacity = parseIntegerField(result.text, L"Design capacity");
    snapshot.fullChargeCapacity = parseIntegerField(result.text, L"Last full charge");
    snapshot.remainingCapacity = parseIntegerField(result.text, L"Remaining capacity");
    snapshot.batteryVoltageMv = parseIntegerField(result.text, L"Present voltage");
    snapshot.batteryCurrentMa = parseIntegerField(result.text, L"Present current");
    snapshot.cycleCount = parseIntegerField(result.text, L"Cycle count");

    if (snapshot.designCapacity > 0 && snapshot.fullChargeCapacity >= 0)
        snapshot.batteryHealth = std::clamp((snapshot.fullChargeCapacity * 100 + snapshot.designCapacity / 2) /
                                                snapshot.designCapacity,
                                            0, 100);
    if (snapshot.fullChargeCapacity > 0 && snapshot.remainingCapacity >= 0)
        snapshot.batteryPercent = std::clamp((snapshot.remainingCapacity * 100 + snapshot.fullChargeCapacity / 2) /
                                                 snapshot.fullChargeCapacity,
                                             0, 100);
    if (snapshot.batteryVoltageMv >= 0)
        snapshot.batteryPowerWatts = std::abs(snapshot.batteryVoltageMv * snapshot.batteryCurrentMa) / 1000000.0;

    if (result.text.find(L"DISCHARGING") != std::wstring::npos) {
        snapshot.batteryState = L"放电中";
        if (snapshot.batteryCurrentMa < 0 && snapshot.remainingCapacity >= 0)
            snapshot.estimatedMinutes = snapshot.remainingCapacity * 60 / -snapshot.batteryCurrentMa;
    } else if (result.text.find(L"CHARGING") != std::wstring::npos) {
        snapshot.batteryState = L"充电中";
        if (snapshot.batteryCurrentMa > 0 && snapshot.fullChargeCapacity >= snapshot.remainingCapacity)
            snapshot.estimatedMinutes = (snapshot.fullChargeCapacity - snapshot.remainingCapacity) * 60 /
                                        snapshot.batteryCurrentMa;
    } else if (result.text.find(L"AC_PRESENT") != std::wstring::npos) {
        snapshot.batteryState = L"已接电源";
    } else {
        snapshot.batteryState = L"电池供电";
    }
}

void parseChargeControl(Snapshot& snapshot, const Result& result) {
    snapshot.charge = result.text;
    if (result.exitCode != 0) return;
    snapshot.chargeSupported = true;
    std::wregex pattern(LR"(Battery sustainer\s*=\s*(on|off)\s*\((-?\d+)%\s*~\s*(-?\d+)%\))",
                        std::regex::icase);
    std::wsmatch match;
    if (std::regex_search(result.text, match, pattern)) {
        snapshot.sustainerEnabled = _wcsicmp(match[1].str().c_str(), L"on") == 0;
        snapshot.chargeLower = std::stoi(match[2].str());
        snapshot.chargeUpper = std::stoi(match[3].str());
    }
}

std::wstring chooseFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) return {};
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择驱动备份保存位置");
    std::wstring selected;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selected = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

std::wstring timestamp() {
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t value[32]{};
    swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    return value;
}

std::filesystem::path chooseReportFile(HWND owner) {
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) return {};
    COMDLG_FILTERSPEC filters[] = {{L"文本报告 (*.txt)", L"*.txt"}};
    dialog->SetFileTypes(1, filters);
    dialog->SetDefaultExtension(L"txt");
    std::wstring fileName = L"CrosEC-Diagnostic-" + timestamp() + L".txt";
    dialog->SetFileName(fileName.c_str());
    dialog->SetTitle(L"保存 CrosEC 诊断报告");

    std::filesystem::path selected;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                selected = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return selected;
}

bool writeUtf8File(const std::filesystem::path& path, const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return false;
    std::string bytes(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}

BackupResult backupDrivers(const std::filesystem::path& selectedRoot) {
    BackupResult result;
    const auto root = selectedRoot / (L"CrosEC-Driver-Backup-" + timestamp());
    const auto drivers = root / L"Drivers";
    std::error_code ec;
    std::filesystem::create_directories(drivers, ec);
    if (ec) {
        result.message = L"无法创建备份目录：\r\n" + root.wstring() + L"\r\n\r\n" + utf8ToWide(ec.message());
        return result;
    }
    result.folder = root.wstring();

    const std::wstring restore =
        L"@echo off\r\n"
        L"setlocal\r\n"
        L"chcp 65001 >nul\r\n"
        L"echo ============================================================\r\n"
        L"echo  WARNING: Installing incompatible drivers can make devices\r\n"
        L"echo  unstable or prevent Windows from starting correctly.\r\n"
        L"echo  Use this only after reinstalling Windows on the SAME machine.\r\n"
        L"echo ============================================================\r\n"
        L"echo.\r\n"
        L"set /p CONFIRM=Type RESTORE to continue: \r\n"
        L"if /I not \"%CONFIRM%\"==\"RESTORE\" (echo Cancelled. & pause & exit /b 2)\r\n"
        L"fltmc >nul 2>&1 || (powershell -NoProfile -Command \"Start-Process -FilePath '%~f0' -Verb RunAs\" & exit /b)\r\n"
        L"echo Restoring third-party drivers...\r\n"
        L"pnputil /add-driver \"%~dp0Drivers\\*.inf\" /subdirs /install\r\n"
        L"set DRIVER_RESULT=%ERRORLEVEL%\r\n"
        L"if exist \"%~dp0Crosec-Tools\\\" (\r\n"
        L"  echo Restoring CROS-EC support tools...\r\n"
        L"  robocopy \"%~dp0Crosec-Tools\" \"%ProgramFiles%\\crosec\" /E /R:1 /W:1 >nul\r\n"
        L")\r\n"
        L"echo.\r\n"
        L"if not \"%DRIVER_RESULT%\"==\"0\" echo Some drivers may not have been restored. pnputil exit code: %DRIVER_RESULT%\r\n"
        L"echo Finished. Restart Windows if requested.\r\n"
        L"pause\r\n"
        L"exit /b %DRIVER_RESULT%\r\n";
    writeUtf8File(root / L"Restore-Drivers.cmd", restore);

    const std::wstring notes =
        L"CrosEC Control - Windows Driver Backup\r\n"
        L"=======================================\r\n\r\n"
        L"Drivers\\             Third-party Driver Store packages exported by pnputil.\r\n"
        L"Crosec-Tools\\         A copy of C:\\Program Files\\crosec when available.\r\n"
        L"Backup.log            Full pnputil export log.\r\n"
        L"Restore-Drivers.cmd   Elevated one-click driver restore script.\r\n\r\n"
        L"After reinstalling Windows on the same machine, inspect the backup and then run\r\n"
        L"Restore-Drivers.cmd as administrator. The script requires typing RESTORE before it acts.\r\n"
        L"Do not run the restore script on a working installation merely to test it.\r\n"
        L"Windows inbox drivers are part of Windows and are not exported by pnputil.\r\n";
    writeUtf8File(root / L"README.txt", notes);

    // ectool/cbmem and related user-mode helpers are not Driver Store files,
    // so preserve them alongside the exported CROS-EC driver package.
    const std::filesystem::path crosec = L"C:\\Program Files\\crosec";
    if (std::filesystem::is_directory(crosec, ec)) {
        ec.clear();
        std::filesystem::copy(crosec, root / L"Crosec-Tools",
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing, ec);
    }
    ec.clear();

    const auto log = root / L"Backup.log";
    std::wstring command = L"/d /s /c \"\"";
    command += L"%SystemRoot%\\System32\\pnputil.exe\" /export-driver * " + quoteArg(drivers.wstring());
    command += L" > " + quoteArg(log.wstring()) + L" 2>&1\"";

    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    execute.hwnd = gWindow;
    execute.lpVerb = L"runas";
    execute.lpFile = L"cmd.exe";
    execute.lpParameters = command.c_str();
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute)) {
        DWORD error = GetLastError();
        result.message = error == ERROR_CANCELLED ? L"用户取消了管理员权限请求。"
                                                  : L"无法启动驱动备份（错误 " + std::to_wstring(error) + L"）。";
        return result;
    }
    WaitForSingleObject(execute.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(execute.hProcess, &exitCode); CloseHandle(execute.hProcess);

    size_t infCount = 0;
    for (std::filesystem::recursive_directory_iterator it(drivers, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && _wcsicmp(it->path().extension().c_str(), L".inf") == 0) ++infCount;
    }
    result.success = exitCode == 0 && infCount > 0;
    if (result.success) {
        result.message = L"驱动备份完成。\r\n\r\n位置：" + root.wstring() +
                         L"\r\n驱动包数量：" + std::to_wstring(infCount) +
                         L"\r\n\r\n重装 Windows 后，以管理员身份运行 Restore-Drivers.cmd 即可恢复。";
    } else {
        result.message = L"驱动备份未成功完成（退出代码 " + std::to_wstring(exitCode) +
                         L"）。\r\n请查看：\r\n" + log.wstring();
    }
    return result;
}

void startDriverBackup(const std::wstring& selectedRoot) {
    EnableWindow(gBackupButton, FALSE);
    setOutput(L"驱动备份", L"正在请求管理员权限并导出第三方驱动，请稍候…");
    std::thread([selectedRoot] {
        HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        auto* result = new BackupResult(backupDrivers(selectedRoot));
        if (!PostMessageW(gWindow, WM_DRIVER_BACKUP, 0, reinterpret_cast<LPARAM>(result))) delete result;
        if (SUCCEEDED(com)) CoUninitialize();
    }).detach();
}

Snapshot collectSnapshot() {
    Snapshot s;
    if (gEctool.empty()) { s.status = L"未找到 ECTOOL"; return s; }
    Result hello = runEctool({L"hello"});
    s.connected = hello.exitCode == 0;
    s.status = s.connected ? L"EC 已连接" : L"EC 连接失败";
    if (!s.connected) { s.status += L" · " + hello.text; return s; }
    auto temps = runEctool({L"temps", L"all"}); s.temps = temps.text; s.maxTemp = parseMaxTemp(temps.text);
    auto fans = runEctool({L"pwmgetfanrpm", L"all"}); s.fans = fans.text; s.rpms = parseRpms(fans.text);
    auto kb = runEctool({L"pwmgetkblight"}); s.keyboard = kb.text; if (kb.exitCode == 0) s.keyboardPercent = parsePercent(kb.text);
    parseBattery(s, runEctool({L"battery"}));
    parseChargeControl(s, runEctool({L"chargecontrol"}));
    return s;
}

void workerMain() {
    while (!gStop) {
        auto* snapshot = new Snapshot(collectSnapshot());
        if (!PostMessageW(gWindow, WM_EC_UPDATE, 0, reinterpret_cast<LPARAM>(snapshot))) delete snapshot;
        for (int i = 0; i < 40 && !gStop && !gRefreshNow; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        gRefreshNow = false;
    }
}

void runAsync(std::wstring title, std::vector<std::wstring> args) {
    std::thread([title = std::move(title), args = std::move(args)] {
        Result r = runEctool(args);
        auto* data = new AsyncResult{title, r.text.empty() ? L"（无输出）" : r.text, r.exitCode != 0};
        if (!PostMessageW(gWindow, WM_EC_RESULT, 0, reinterpret_cast<LPARAM>(data))) delete data;
        gRefreshNow = true;
    }).detach();
}

std::wstring commandText(const std::vector<std::wstring>& args) {
    std::wstring text = L"ectool";
    for (const auto& arg : args) text += L" " + arg;
    return text;
}

std::wstring collectDiagnostics(const std::vector<DiagnosticCommand>& commands) {
    std::wstring output;
    for (const auto& command : commands) {
        Result result = runEctool(command.args);
        output += L"===== " + command.title + L" =====\r\n";
        output += L"命令：" + commandText(command.args) + L"\r\n";
        output += L"退出代码：" + std::to_wstring(result.exitCode) + L"\r\n";
        output += result.text.empty() ? L"（无输出）\r\n\r\n" : result.text + L"\r\n";
    }
    return output;
}

void runDiagnosticGroup(std::wstring title, std::vector<DiagnosticCommand> commands) {
    setOutput(title, L"正在收集只读诊断信息，请稍候…");
    std::thread([title = std::move(title), commands = std::move(commands)] {
        std::wstring text = collectDiagnostics(commands);
        auto* data = new AsyncResult{title, std::move(text), false};
        if (!PostMessageW(gWindow, WM_EC_RESULT, 0, reinterpret_cast<LPARAM>(data))) delete data;
    }).detach();
}

std::vector<DiagnosticCommand> fullDiagnosticCommands() {
    return {
        {L"EC 版本", {L"version"}},
        {L"EC 支持能力", {L"inventory"}},
        {L"系统信息", {L"sysinfo"}},
        {L"EC 运行状态", {L"uptimeinfo"}},
        {L"EC Panic", {L"panicinfo"}},
        {L"电池", {L"battery"}},
        {L"充电策略", {L"chargecontrol"}},
        {L"温度", {L"temps", L"all"}},
        {L"温度传感器", {L"tempsinfo", L"all"}},
        {L"热管理阈值", {L"thermalget"}},
        {L"风扇数量", {L"pwmgetnumfans"}},
        {L"风扇转速", {L"pwmgetfanrpm", L"all"}},
        {L"键盘背光", {L"pwmgetkblight"}},
        {L"物理开关", {L"switches"}},
        {L"设备模式", {L"mkbpget", L"switches"}},
        {L"USB-C 端口 0 电源", {L"usbpdpower", L"0"}},
        {L"USB-C 端口 1 电源", {L"usbpdpower", L"1"}},
        {L"USB-C 端口 0 状态", {L"typecstatus", L"0"}},
        {L"USB-C 端口 1 状态", {L"typecstatus", L"1"}},
        {L"USB-C MUX", {L"usbpdmuxinfo"}},
        {L"主板版本", {L"boardversion"}},
        {L"芯片", {L"chipinfo"}},
        {L"Flash 信息", {L"flashinfo"}},
        {L"协议", {L"protoinfo"}},
        {L"Port 80 启动记录", {L"port80read"}}
    };
}

void startDiagnosticReport(const std::filesystem::path& path) {
    EnableWindow(gReportButton, FALSE);
    setOutput(L"诊断报告", L"正在执行只读命令并生成报告，请稍候…");
    std::thread([path] {
        std::wstring report =
            L"CrosEC Control 诊断报告\r\n"
            L"========================================\r\n"
            L"生成时间：" + timestamp() + L"\r\n"
            L"ECTOOL：" + gEctool.wstring() + L"\r\n\r\n"
            L"隐私提示：本报告可能包含电池型号、序列号、固件版本和设备状态。\r\n"
            L"所有命令均为只读查询；单条命令失败不会中止报告。\r\n\r\n";
        report += collectDiagnostics(fullDiagnosticCommands());

        auto* result = new ReportResult;
        result->path = path.wstring();
        result->success = writeUtf8File(path, report);
        result->message = result->success ? L"诊断报告已保存：\r\n" + path.wstring()
                                          : L"无法写入诊断报告：\r\n" + path.wstring();
        if (!PostMessageW(gWindow, WM_DIAGNOSTIC_REPORT, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

HWND addControl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id,
                HFONT font = nullptr, UiSurface surface = UiSurface::Background) {
    HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                                   gWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : gFont), TRUE);
    SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    gUiItems.push_back({control, surface});
    return control;
}
HWND addLabel(const wchar_t* text, int x, int y, int w, int h, int id = 0, HFONT font = nullptr,
              UiSurface surface = UiSurface::Background) {
    return addControl(L"STATIC", text, SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX, x, y, w, h, id, font, surface);
}
HWND addButton(const wchar_t* text, int x, int y, int w, int h, int id) {
    // Owner-draw keeps dark-theme label contrast consistent across all buttons.
    return addControl(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
}

void drawOwnerButton(const DRAWITEMSTRUCT* item) {
    if (!item || !item->hDC) return;
    const bool enabled = (item->itemState & ODS_DISABLED) == 0;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool focus = (item->itemState & ODS_FOCUS) != 0;
    const bool activeTab = (item->hwndItem == gTabControl && gActiveTab == 0) ||
                           (item->hwndItem == gTabDiag && gActiveTab == 1);
    const COLORREF fill = !enabled ? RGB(30, 33, 38)
                                   : (pressed ? RGB(50, 58, 68)
                                              : (activeTab ? RGB(42, 52, 68) : RGB(40, 45, 52)));
    const COLORREF border = (focus || pressed || activeTab) ? kAccent : kBorder;

    HBRUSH fillBrush = CreateSolidBrush(fill);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(item->hDC, fillBrush);
    HGDIOBJ oldPen = SelectObject(item->hDC, borderPen);
    Rectangle(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom);
    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(fillBrush);
    DeleteObject(borderPen);

    wchar_t text[128]{};
    GetWindowTextW(item->hwndItem, text, 128);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, enabled ? RGB(245, 247, 250) : RGB(120, 126, 136));
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(item->hDC, gFont ? gFont : GetStockObject(DEFAULT_GUI_FONT)));
    RECT textRc = item->rcItem;
    if (pressed) OffsetRect(&textRc, 1, 1);
    DrawTextW(item->hDC, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(item->hDC, oldFont);
}

void trackPage(std::vector<HWND>& page, HWND control) {
    if (control) page.push_back(control);
}

void setText(HWND hwnd, const std::wstring& text) { SetWindowTextW(hwnd, text.c_str()); }
void updateTabButtons() {
    // Owner-draw buttons: active tab is emphasized in drawOwnerButton via focus/state + invalidate.
    if (gTabControl) {
        SendMessageW(gTabControl, BM_SETSTATE, gActiveTab == 0 ? TRUE : FALSE, 0);
        InvalidateRect(gTabControl, nullptr, TRUE);
    }
    if (gTabDiag) {
        SendMessageW(gTabDiag, BM_SETSTATE, gActiveTab == 1 ? TRUE : FALSE, 0);
        InvalidateRect(gTabDiag, nullptr, TRUE);
    }
}

void setActiveTab(int tab) {
    if (tab != 0 && tab != 1) return;
    gActiveTab = tab;
    updateTabButtons();
    applyTabVisibility();
    if (gWindow) layout(gWindow);
}

void setOutput(const std::wstring& title, const std::wstring& text) {
    setText(gDiagTitle, title);
    SetWindowTextW(gOutput, text.c_str());
    if (gActiveTab != 1) setActiveTab(1);
}
void commandResult(const std::wstring& title, const std::vector<std::wstring>& args, const std::wstring& mode = {}) {
    if (!mode.empty()) { setText(gMode, mode); gManualFan = mode != L"自动"; }
    runAsync(title, args);
}

COLORREF temperatureColor(int tempC) {
    if (tempC < 0) return kMuted;
    if (tempC >= 90) return kDanger;
    if (tempC >= 80) return kWarn;
    if (tempC >= 70) return RGB(220, 200, 90);
    return kGreen;
}

COLORREF usageColor(int percent) {
    if (percent < 0) return kMuted;
    if (percent >= 90) return kDanger;
    if (percent >= 75) return kWarn;
    if (percent >= 50) return RGB(220, 200, 90);
    return kGreen;
}

ULONGLONG fileTimeToUll(FILETIME ft) {
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Human-readable capacity for system metric captions (RAM / disk).
std::wstring formatCapacity(ULONGLONG bytes) {
    constexpr double kMiB = 1024.0 * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    constexpr double kTiB = kGiB * 1024.0;
    wchar_t buf[32]{};
    if (bytes >= static_cast<ULONGLONG>(kTiB)) {
        swprintf_s(buf, L"%.1f TB", static_cast<double>(bytes) / kTiB);
    } else if (bytes >= static_cast<ULONGLONG>(kGiB)) {
        const double gib = static_cast<double>(bytes) / kGiB;
        if (gib >= 100.0)
            swprintf_s(buf, L"%.0f GB", gib);
        else if (std::fabs(gib - std::round(gib)) < 0.05)
            swprintf_s(buf, L"%.0f GB", std::round(gib));
        else
            swprintf_s(buf, L"%.1f GB", gib);
    } else {
        swprintf_s(buf, L"%.0f MB", static_cast<double>(bytes) / kMiB);
    }
    return buf;
}

std::wstring collapseWhitespace(std::wstring text) {
    std::wstring out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (wchar_t ch : text) {
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(L' ');
            pendingSpace = false;
        }
        out.push_back(ch);
    }
    return out;
}

std::wstring stripCpuNoise(std::wstring name) {
    const wchar_t* tokens[] = {
        L"(R)", L"(TM)", L"(C)", L"®", L"™", L"CPU", L"Processor", L"with Radeon Graphics",
        L"with Radeon Vega Graphics", L"Genuine Intel",
    };
    for (const wchar_t* token : tokens) {
        for (;;) {
            const size_t pos = name.find(token);
            if (pos == std::wstring::npos) break;
            name.erase(pos, wcslen(token));
        }
    }
    const size_t at = name.find(L" @ ");
    if (at != std::wstring::npos)
        name.erase(at);
    // Drop leading generation tags: "12th Gen Intel Core ..."
    static const std::wregex genTag(LR"(^\d+(?:st|nd|rd|th)\s+Gen\s+)", std::regex_constants::icase);
    name = std::regex_replace(name, genTag, L"");
    for (const wchar_t* brand : {L"Intel ", L"AMD ", L"Core "}) {
        if (name.size() >= wcslen(brand) && _wcsnicmp(name.c_str(), brand, wcslen(brand)) == 0)
            name.erase(0, wcslen(brand));
    }
    return collapseWhitespace(name);
}

// Reduce long registry names like "Intel(R) Core(TM) i3-8100 CPU @ 3.60GHz" to "i3-8100".
std::wstring shortenCpuModel(const std::wstring& rawName) {
    if (rawName.empty()) return {};
    const std::wstring cleaned = stripCpuNoise(rawName);
    std::wsmatch match;

    static const std::wregex intelCore(LR"(\bi([3579])[\s\-]?(\d{3,5}[A-Za-z0-9]*)\b)", std::regex_constants::icase);
    if (std::regex_search(cleaned, match, intelCore))
        return L"i" + match[1].str() + L"-" + match[2].str();

    static const std::wregex ryzen(LR"(\bRyzen\s+\d+\s+\d{3,5}[A-Za-z0-9]*\b)", std::regex_constants::icase);
    if (std::regex_search(cleaned, match, ryzen))
        return match.str();

    static const std::wregex otherIntel(LR"(\b(?:Celeron|Pentium(?:\s+Silver)?|Atom|Xeon)\s+[A-Za-z0-9\-]+\b)",
                                        std::regex_constants::icase);
    if (std::regex_search(cleaned, match, otherIntel))
        return match.str();

    static const std::wregex apple(LR"(\bApple\s+M\d+(?:\s+(?:Pro|Max|Ultra))?\b)", std::regex_constants::icase);
    if (std::regex_search(cleaned, match, apple))
        return match.str();

    static const std::wregex snapdragon(LR"(\bSnapdragon(?:\s+\w+){0,3}\b)", std::regex_constants::icase);
    if (std::regex_search(cleaned, match, snapdragon))
        return match.str();

    std::wstring fallback = cleaned;
    if (fallback.size() > 22)
        fallback = fallback.substr(0, 21) + L"…";
    return fallback;
}

std::wstring readCpuModel() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t name[256]{};
    DWORD size = sizeof(name);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(name), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || name[0] == 0)
        return {};
    return shortenCpuModel(name);
}

std::wstring asciiFieldAt(const BYTE* base, DWORD offset, size_t maxLen = 64) {
    if (!base || offset == 0) return {};
    const char* field = reinterpret_cast<const char*>(base + offset);
    size_t begin = 0;
    while (begin < maxLen && field[begin] == ' ') ++begin;
    size_t end = begin;
    while (end < maxLen && field[end] != 0) ++end;
    while (end > begin && field[end - 1] == ' ') --end;
    if (end <= begin) return {};
    const int wideLen = MultiByteToWideChar(CP_ACP, 0, field + begin, static_cast<int>(end - begin), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring out(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_ACP, 0, field + begin, static_cast<int>(end - begin), out.data(), wideLen);
    return collapseWhitespace(out);
}

std::wstring shortenDiskModel(std::wstring model) {
    model = collapseWhitespace(std::move(model));
    if (model.empty()) return {};
    // Drop very common noisy prefixes that waste caption space.
    const wchar_t* prefixes[] = {L"NVMe ", L"SCSI ", L"USB "};
    for (const wchar_t* prefix : prefixes) {
        if (model.size() >= wcslen(prefix) && _wcsnicmp(model.c_str(), prefix, wcslen(prefix)) == 0)
            model.erase(0, wcslen(prefix));
    }
    if (model.size() > 18)
        model = model.substr(0, 17) + L"…";
    return model;
}

std::wstring readSystemDiskModel(wchar_t diskLetter) {
    if (diskLetter == 0) return {};
    wchar_t volumePath[] = L"\\\\.\\C:";
    volumePath[4] = diskLetter;
    HANDLE volume = CreateFileW(volumePath, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return {};

    BYTE extentsBuffer[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 4]{};
    DWORD bytesReturned = 0;
    const BOOL gotExtents = DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                            nullptr, 0, extentsBuffer, sizeof(extentsBuffer),
                                            &bytesReturned, nullptr);
    CloseHandle(volume);
    if (!gotExtents) return {};

    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(extentsBuffer);
    if (extents->NumberOfDiskExtents < 1) return {};

    wchar_t physicalPath[64]{};
    swprintf_s(physicalPath, L"\\\\.\\PhysicalDrive%u", extents->Extents[0].DiskNumber);
    HANDLE disk = CreateFileW(physicalPath, 0,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE) return {};

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    BYTE descriptorBuffer[1024]{};
    const BOOL gotProperty = DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY,
                                             &query, sizeof(query),
                                             descriptorBuffer, sizeof(descriptorBuffer),
                                             &bytesReturned, nullptr);
    CloseHandle(disk);
    if (!gotProperty) return {};

    const auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(descriptorBuffer);
    std::wstring vendor = asciiFieldAt(descriptorBuffer, desc->VendorIdOffset);
    std::wstring product = asciiFieldAt(descriptorBuffer, desc->ProductIdOffset);
    std::wstring model;
    if (!vendor.empty() && !product.empty()) {
        // Avoid "Samsung Samsung ..." style duplication.
        if (_wcsnicmp(product.c_str(), vendor.c_str(), vendor.size()) == 0)
            model = product;
        else
            model = vendor + L" " + product;
    } else {
        model = !product.empty() ? product : vendor;
    }
    return shortenDiskModel(std::move(model));
}

// CPU / disk model strings are static for the process lifetime.
struct CachedHardwareSpecs {
    std::wstring cpuModel;
    std::wstring diskModel;
    wchar_t diskLetter = 0;
    bool cpuLoaded = false;
    bool diskLoaded = false;
};

CachedHardwareSpecs gHardwareSpecs;

const std::wstring& cachedCpuModel() {
    if (!gHardwareSpecs.cpuLoaded) {
        gHardwareSpecs.cpuModel = readCpuModel();
        gHardwareSpecs.cpuLoaded = true;
    }
    return gHardwareSpecs.cpuModel;
}

const std::wstring& cachedDiskModel(wchar_t diskLetter) {
    if (!gHardwareSpecs.diskLoaded || gHardwareSpecs.diskLetter != diskLetter) {
        gHardwareSpecs.diskModel = readSystemDiskModel(diskLetter);
        gHardwareSpecs.diskLetter = diskLetter;
        gHardwareSpecs.diskLoaded = true;
    }
    return gHardwareSpecs.diskModel;
}

SystemMetrics sampleSystemMetrics() {
    SystemMetrics metrics;

    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    metrics.cpuLogicalCount = static_cast<int>(systemInfo.dwNumberOfProcessors);
    metrics.cpuModel = cachedCpuModel();

    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) {
        metrics.memoryPercent = static_cast<int>(memory.dwMemoryLoad);
        metrics.memoryTotalBytes = memory.ullTotalPhys;
    }

    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) > 0 && systemDir[0] != 0)
        metrics.diskLetter = systemDir[0];
    wchar_t root[] = L"C:\\";
    root[0] = metrics.diskLetter;
    ULARGE_INTEGER freeBytes{}, totalBytes{}, totalFree{};
    if (GetDiskFreeSpaceExW(root, &freeBytes, &totalBytes, &totalFree) && totalBytes.QuadPart > 0) {
        const ULONGLONG used = totalBytes.QuadPart - freeBytes.QuadPart;
        metrics.diskPercent = static_cast<int>((used * 100ull) / totalBytes.QuadPart);
        metrics.diskTotalBytes = totalBytes.QuadPart;
    }
    metrics.diskModel = cachedDiskModel(metrics.diskLetter);

    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        if (gHaveCpuSample) {
            const ULONGLONG idleDelta = fileTimeToUll(idle) - fileTimeToUll(gPrevIdle);
            const ULONGLONG kernelDelta = fileTimeToUll(kernel) - fileTimeToUll(gPrevKernel);
            const ULONGLONG userDelta = fileTimeToUll(user) - fileTimeToUll(gPrevUser);
            // Kernel time includes idle time on Windows.
            const ULONGLONG total = kernelDelta + userDelta;
            if (total > 0)
                metrics.cpuPercent = static_cast<int>(((total - idleDelta) * 100ull) / total);
        }
        gPrevIdle = idle;
        gPrevKernel = kernel;
        gPrevUser = user;
        gHaveCpuSample = true;
    }
    return metrics;
}

void applySystemMetrics(const SystemMetrics& metrics) {
    gCpuColor = usageColor(metrics.cpuPercent);
    gMemColor = usageColor(metrics.memoryPercent);
    gDiskColor = usageColor(metrics.diskPercent);

    if (!metrics.cpuModel.empty()) {
        std::wstring caption = L"CPU · " + metrics.cpuModel;
        if (metrics.cpuLogicalCount > 0 && metrics.cpuModel.size() <= 14)
            caption += L" · " + std::to_wstring(metrics.cpuLogicalCount) + L" 线程";
        setText(gCpuCaption, caption);
    } else if (metrics.cpuLogicalCount > 0) {
        setText(gCpuCaption, L"CPU · " + std::to_wstring(metrics.cpuLogicalCount) + L" 线程");
    } else {
        setText(gCpuCaption, L"CPU");
    }
    setText(gCpu, metrics.cpuPercent >= 0 ? std::to_wstring(metrics.cpuPercent) + L" %" : L"N/A");

    if (metrics.memoryTotalBytes > 0)
        setText(gMemCaption, L"内存 · " + formatCapacity(metrics.memoryTotalBytes));
    else
        setText(gMemCaption, L"内存");
    setText(gMem, metrics.memoryPercent >= 0 ? std::to_wstring(metrics.memoryPercent) + L" %" : L"N/A");

    if (metrics.diskPercent >= 0) {
        std::wstring label(1, metrics.diskLetter);
        std::wstring caption = L"磁盘 " + label + L":";
        if (!metrics.diskModel.empty())
            caption += L" · " + metrics.diskModel;
        if (metrics.diskTotalBytes > 0)
            caption += L" · " + formatCapacity(metrics.diskTotalBytes);
        setText(gDiskCaption, caption);
        setText(gDisk, std::to_wstring(metrics.diskPercent) + L" %");
    } else {
        setText(gDiskCaption, L"磁盘");
        setText(gDisk, L"N/A");
    }
    if (gCpu) InvalidateRect(gCpu, nullptr, FALSE);
    if (gMem) InvalidateRect(gMem, nullptr, FALSE);
    if (gDisk) InvalidateRect(gDisk, nullptr, FALSE);
    if (gCpuCaption) InvalidateRect(gCpuCaption, nullptr, FALSE);
    if (gMemCaption) InvalidateRect(gMemCaption, nullptr, FALSE);
    if (gDiskCaption) InvalidateRect(gDiskCaption, nullptr, FALSE);
}

std::wstring formatFanText(const std::vector<int>& rpms, int cardWidth) {
    if (rpms.empty()) return L"N/A";
    std::wstring text;
    for (size_t i = 0; i < rpms.size(); ++i) {
        if (i) text += cardWidth < 210 ? L"/" : L" / ";
        if (cardWidth >= 170) text += L"F" + std::to_wstring(i + 1) + (cardWidth < 210 ? L" " : L" ");
        text += std::to_wstring(rpms[i]);
    }
    if (cardWidth >= 150) text += L" RPM";
    return text;
}

void applyTabVisibility() {
    const bool control = gActiveTab == 0;
    for (HWND hwnd : gControlPage) ShowWindow(hwnd, control ? SW_SHOW : SW_HIDE);
    for (HWND hwnd : gDiagPage) ShowWindow(hwnd, control ? SW_HIDE : SW_SHOW);
}

int uiScale(HWND hwnd, int value) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

void place(HWND hwnd, int x, int y, int w, int h) {
    if (hwnd) MoveWindow(hwnd, x, y, std::max(0, w), std::max(0, h), TRUE);
}

void fillRoundRect(HDC dc, const RECT& rc, COLORREF fill, COLORREF border) {
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void paintUi(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, gBgBrush);
    fillRoundRect(dc, gOverviewPanel, kPanel, kBorder);
    fillRoundRect(dc, gCardTemp, kPanel2, kBorder);
    fillRoundRect(dc, gCardFans, kPanel2, kBorder);
    fillRoundRect(dc, gCardMode, kPanel2, kBorder);
    fillRoundRect(dc, gCardCpu, kPanel2, kBorder);
    fillRoundRect(dc, gCardMem, kPanel2, kBorder);
    fillRoundRect(dc, gCardDisk, kPanel2, kBorder);
    if (gActiveTab == 0) fillRoundRect(dc, gControlPanel, kPanel, kBorder);
    else fillRoundRect(dc, gDiagPanel, kPanel, kBorder);
    fillRoundRect(dc, gStatusBar, kPanel2, kBorder);

}

void createUi(HWND hwnd) {
    gWindow = hwnd;
    gUiItems.clear();
    gControlPage.clear();
    gDiagPage.clear();

    gFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    gFontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gFontLarge = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gFontTitle = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gFontStatus = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    gFontMono = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Cascadia Mono");
    gBgBrush = CreateSolidBrush(kBg);
    gPanelBrush = CreateSolidBrush(kPanel);
    gPanel2Brush = CreateSolidBrush(kPanel2);
    gEditBrush = CreateSolidBrush(RGB(22, 25, 29));
    gStatusOkBrush = CreateSolidBrush(RGB(24, 48, 38));
    gStatusBadBrush = CreateSolidBrush(RGB(52, 30, 30));
    gStatusWaitBrush = CreateSolidBrush(RGB(40, 42, 48));

    // Compact header: no duplicate large app title (title bar already has it).
    gHeaderSub = addLabel(L"ChromeOS EC 控制面板", 0, 0, 10, 10, ID_HEADER_SUB, gFontSmall, UiSurface::Background);
    gStatus = addLabel(L"正在检测 EC…", 0, 0, 10, 10, ID_STATUS, gFontStatus, UiSurface::Background);
    gRefreshBtn = addButton(L"立即刷新", 0, 0, 10, 10, ID_REFRESH);

    gOverviewTitle = addLabel(L"设备概览", 0, 0, 10, 10, ID_OVERVIEW_TITLE, gFontTitle, UiSurface::Panel);
    gTempCaption = addLabel(L"最高温度", 0, 0, 10, 10, ID_TEMP_CAPTION, gFontSmall, UiSurface::Panel);
    gTemp = addLabel(L"-- °C", 0, 0, 10, 10, ID_TEMP, gFontLarge, UiSurface::Panel);
    gFansCaption = addLabel(L"风扇转速", 0, 0, 10, 10, ID_FANS_CAPTION, gFontSmall, UiSurface::Panel);
    gFans = addLabel(L"-- RPM", 0, 0, 10, 10, ID_FANS, gFontLarge, UiSurface::Panel);
    gModeCaption = addLabel(L"当前模式", 0, 0, 10, 10, ID_MODE_CAPTION, gFontSmall, UiSurface::Panel);
    gMode = addLabel(L"自动", 0, 0, 10, 10, ID_MODE, gFontLarge, UiSurface::Panel);
    gBatteryHealth = addLabel(L"电池健康：正在读取…", 0, 0, 10, 10, 0, gFontSmall, UiSurface::Panel);
    gBatteryPower = addLabel(L"电量：--", 0, 0, 10, 10, 0, gFontSmall, UiSurface::Panel);
    gBatteryTime = addLabel(L"预计时间：--", 0, 0, 10, 10, 0, gFontSmall, UiSurface::Panel);
    gCpuCaption = addLabel(L"CPU", 0, 0, 10, 10, ID_CPU_CAPTION, gFontSmall, UiSurface::Panel);
    gCpu = addLabel(L"-- %", 0, 0, 10, 10, ID_CPU, gFontLarge, UiSurface::Panel);
    gMemCaption = addLabel(L"内存", 0, 0, 10, 10, ID_MEM_CAPTION, gFontSmall, UiSurface::Panel);
    gMem = addLabel(L"-- %", 0, 0, 10, 10, ID_MEM, gFontLarge, UiSurface::Panel);
    gDiskCaption = addLabel(L"磁盘", 0, 0, 10, 10, ID_DISK_CAPTION, gFontSmall, UiSurface::Panel);
    gDisk = addLabel(L"-- %", 0, 0, 10, 10, ID_DISK, gFontLarge, UiSurface::Panel);

    gTabControl = addButton(L"控制", 0, 0, 10, 10, ID_TAB_CONTROL);
    gTabDiag = addButton(L"诊断", 0, 0, 10, 10, ID_TAB_DIAG);

    gFanTitle = addLabel(L"风扇控制", 0, 0, 10, 10, ID_FAN_TITLE, gFontTitle, UiSurface::Panel);
    gBtnAuto = addButton(L"自动", 0, 0, 10, 10, ID_AUTO);
    gBtnOff = addButton(L"关闭", 0, 0, 10, 10, ID_OFF);
    gBtnMax = addButton(L"最大", 0, 0, 10, 10, ID_MAX);
    gBtnCustom = addButton(L"自定义", 0, 0, 10, 10, ID_CUSTOM);
    gFanValue = addLabel(L"50%", 0, 0, 10, 10, ID_FAN_VALUE, gFont, UiSurface::Panel);
    gFanSlider = addControl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS | WS_TABSTOP,
                            0, 0, 10, 10, ID_FAN_SLIDER, gFont, UiSurface::Panel);
    SendMessageW(gFanSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(gFanSlider, TBM_SETPOS, TRUE, 50);

    gKbTitle = addLabel(L"键盘背光", 0, 0, 10, 10, ID_KB_TITLE, gFontTitle, UiSurface::Panel);
    gKbSlider = addControl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS | WS_TABSTOP,
                           0, 0, 10, 10, ID_KB_SLIDER, gFont, UiSurface::Panel);
    SendMessageW(gKbSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    gKbValue = addLabel(L"N/A", 0, 0, 10, 10, ID_KB_VALUE, gFont, UiSurface::Panel);

    gChargeTitle = addLabel(L"电池充电上限", 0, 0, 10, 10, ID_CHARGE_TITLE, gFontTitle, UiSurface::Panel);
    gChargeSlider = addControl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS | WS_TABSTOP,
                               0, 0, 10, 10, ID_CHARGE_SLIDER, gFont, UiSurface::Panel);
    SendMessageW(gChargeSlider, TBM_SETRANGE, TRUE, MAKELPARAM(50, 100));
    SendMessageW(gChargeSlider, TBM_SETPOS, TRUE, 80);
    gChargeValue = addLabel(L"80%", 0, 0, 10, 10, ID_CHARGE_VALUE, gFont, UiSurface::Panel);
    // Code-point literals avoid any source-encoding ambiguity for these labels.
    static const wchar_t kChargeApply[] = {0x5E94, 0x7528, 0x4E0A, 0x9650, 0};           // 应用上限
    static const wchar_t kChargeReset[] = {0x6062, 0x590D, L' ', L'1', L'0', L'0', L'%', 0}; // 恢复 100%
    gChargeApply = addButton(kChargeApply, 0, 0, 10, 10, ID_CHARGE_APPLY);
    gChargeReset = addButton(kChargeReset, 0, 0, 10, 10, ID_CHARGE_RESET);
    gChargeStatus = addLabel(L"正在读取充电策略…", 0, 0, 10, 10, ID_CHARGE_STATUS, gFontSmall, UiSurface::Panel);

    for (HWND control : {gFanTitle, gBtnAuto, gBtnOff, gBtnMax, gBtnCustom, gFanValue, gFanSlider,
                         gKbTitle, gKbSlider, gKbValue, gChargeTitle, gChargeSlider, gChargeValue,
                         gChargeApply, gChargeReset, gChargeStatus}) {
        trackPage(gControlPage, control);
    }

    gDiagSection = addLabel(L"只读诊断与备份", 0, 0, 10, 10, ID_DIAG_SECTION, gFontTitle, UiSurface::Panel);
    gBtnBattery = addButton(L"电池详情", 0, 0, 10, 10, ID_BATTERY);
    gBtnUsb = addButton(L"USB-C / PD", 0, 0, 10, 10, ID_USB_C);
    gBtnEcStatus = addButton(L"EC 运行状态", 0, 0, 10, 10, ID_EC_STATUS);
    gBtnDevice = addButton(L"设备模式", 0, 0, 10, 10, ID_DEVICE_STATUS);
    gBtnFirmware = addButton(L"固件与硬件", 0, 0, 10, 10, ID_FIRMWARE_INFO);
    gBtnSensors = addButton(L"传感器与温控", 0, 0, 10, 10, ID_SENSORS);
    gBtnProtocol = addButton(L"协议信息", 0, 0, 10, 10, ID_PROTOCOL);
    gBackupButton = addButton(L"备份驱动", 0, 0, 10, 10, ID_DRIVER_BACKUP);
    gReportButton = addButton(L"导出诊断报告", 0, 0, 10, 10, ID_DIAGNOSTIC_REPORT);
    gBtnCopy = addButton(L"复制输出", 0, 0, 10, 10, ID_COPY);
    gDiagTitle = addLabel(L"诊断输出", 0, 0, 10, 10, ID_DIAG_TITLE, gFont, UiSurface::Panel);
    gOutput = addControl(L"EDIT", L"选择上方项目以查看详细信息。",
                         ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY |
                             WS_VSCROLL | WS_HSCROLL | WS_BORDER,
                         0, 0, 10, 10, ID_OUTPUT, gFontMono, UiSurface::Panel);
    SendMessageW(gOutput, EM_SETLIMITTEXT, 1024 * 1024, 0);

    for (HWND control : {gDiagSection, gBtnBattery, gBtnUsb, gBtnEcStatus, gBtnDevice, gBtnFirmware,
                         gBtnSensors, gBtnProtocol, gBackupButton, gReportButton, gBtnCopy, gDiagTitle, gOutput}) {
        trackPage(gDiagPage, control);
    }

    gPath = addLabel(L"ECTOOL：正在搜索…", 0, 0, 10, 10, ID_PATH, gFontSmall, UiSurface::Panel);

    gActiveTab = 0;
    updateTabButtons();
    applyTabVisibility();
}

void layout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int pad = uiScale(hwnd, kPad);
    const int minW = uiScale(hwnd, kMinClientW);
    const int minH = uiScale(hwnd, kMinClientH);
    const int headerH = uiScale(hwnd, kHeaderH);
    const int overviewH = uiScale(hwnd, kOverviewH);
    const int width = std::max(minW, static_cast<int>(rc.right));
    const int height = std::max(minH, static_cast<int>(rc.bottom));
    const int contentW = width - pad * 2;
    const int statusH = uiScale(hwnd, 28);
    int y = uiScale(hwnd, 12);

    place(gHeaderSub, pad, y + uiScale(hwnd, 4), std::max(uiScale(hwnd, 180), contentW - uiScale(hwnd, 320)), uiScale(hwnd, 22));
    place(gRefreshBtn, width - pad - uiScale(hwnd, 108), y, uiScale(hwnd, 108), uiScale(hwnd, 32));
    place(gStatus, width - pad - uiScale(hwnd, 108) - uiScale(hwnd, 16) - uiScale(hwnd, 158), y + uiScale(hwnd, 2),
          uiScale(hwnd, 158), uiScale(hwnd, 28));
    y += headerH;

    gOverviewPanel = {pad, y, width - pad, y + overviewH};
    place(gOverviewTitle, pad + uiScale(hwnd, 14), y + uiScale(hwnd, 10), uiScale(hwnd, 200), uiScale(hwnd, 26));

    const int cardGap = uiScale(hwnd, 10);
    const int cardY = y + uiScale(hwnd, 42);
    const int cardH = uiScale(hwnd, 64);
    const int cardW = (contentW - uiScale(hwnd, 28) - cardGap * 2) / 3;
    gCardTemp = {pad + uiScale(hwnd, 14), cardY, pad + uiScale(hwnd, 14) + cardW, cardY + cardH};
    gCardFans = {gCardTemp.right + cardGap, cardY, gCardTemp.right + cardGap + cardW, cardY + cardH};
    gCardMode = {gCardFans.right + cardGap, cardY, gCardFans.right + cardGap + cardW, cardY + cardH};

    auto placeCard = [&](const RECT& card, HWND caption, HWND value) {
        place(caption, card.left + uiScale(hwnd, 12), card.top + uiScale(hwnd, 8),
              card.right - card.left - uiScale(hwnd, 24), uiScale(hwnd, 18));
        place(value, card.left + uiScale(hwnd, 12), card.top + uiScale(hwnd, 28),
              card.right - card.left - uiScale(hwnd, 24), uiScale(hwnd, 36));
    };
    placeCard(gCardTemp, gTempCaption, gTemp);
    placeCard(gCardFans, gFansCaption, gFans);
    placeCard(gCardMode, gModeCaption, gMode);

    const int batY = cardY + cardH + uiScale(hwnd, 8);
    const int batW = (contentW - uiScale(hwnd, 28)) / 3;
    place(gBatteryHealth, pad + uiScale(hwnd, 14), batY, batW - uiScale(hwnd, 8), uiScale(hwnd, 20));
    place(gBatteryPower, pad + uiScale(hwnd, 14) + batW, batY, batW - uiScale(hwnd, 8), uiScale(hwnd, 20));
    place(gBatteryTime, pad + uiScale(hwnd, 14) + batW * 2, batY, batW - uiScale(hwnd, 8), uiScale(hwnd, 20));

    const int sysCardY = batY + uiScale(hwnd, 24);
    const int sysCardH = uiScale(hwnd, 54);
    gCardCpu = {pad + uiScale(hwnd, 14), sysCardY, pad + uiScale(hwnd, 14) + cardW, sysCardY + sysCardH};
    gCardMem = {gCardCpu.right + cardGap, sysCardY, gCardCpu.right + cardGap + cardW, sysCardY + sysCardH};
    gCardDisk = {gCardMem.right + cardGap, sysCardY, gCardMem.right + cardGap + cardW, sysCardY + sysCardH};
    placeCard(gCardCpu, gCpuCaption, gCpu);
    placeCard(gCardMem, gMemCaption, gMem);
    placeCard(gCardDisk, gDiskCaption, gDisk);

    setText(gFans, formatFanText(gLastRpms, MulDiv(cardW, 96, static_cast<int>(GetDpiForWindow(hwnd) ? GetDpiForWindow(hwnd) : 96))));

    // Path bar first so overview/control content can stay above it.
    gStatusBar = {pad, height - pad - statusH + uiScale(hwnd, 2), width - pad, height - pad + uiScale(hwnd, 4)};
    place(gPath, gStatusBar.left + uiScale(hwnd, 12), gStatusBar.top + uiScale(hwnd, 4),
          contentW - uiScale(hwnd, 24), uiScale(hwnd, 20));
    const int contentBottom = gStatusBar.top - uiScale(hwnd, 8);

    y += overviewH + uiScale(hwnd, 10);
    place(gTabControl, pad, y, uiScale(hwnd, 88), uiScale(hwnd, 32));
    place(gTabDiag, pad + uiScale(hwnd, 92), y, uiScale(hwnd, 88), uiScale(hwnd, 32));
    y += uiScale(hwnd, 38);

    gControlPanel = {pad, y, width - pad, contentBottom};
    gDiagPanel = gControlPanel;
    const int pageX = pad + uiScale(hwnd, 14);
    const int pageY = y + uiScale(hwnd, 10);
    const int pageW = std::max(uiScale(hwnd, 200), contentW - uiScale(hwnd, 28));

    if (gActiveTab == 0) {
        int py = pageY;
        place(gFanTitle, pageX, py, uiScale(hwnd, 100), uiScale(hwnd, 22));
        py += uiScale(hwnd, 24);
        const int btnGap = uiScale(hwnd, 8);
        const int btnW = std::max(uiScale(hwnd, 72), (pageW - btnGap * 3) / 4);
        const int fanBtnH = uiScale(hwnd, 34);
        place(gBtnAuto, pageX, py, btnW, fanBtnH);
        place(gBtnOff, pageX + (btnW + btnGap), py, btnW, fanBtnH);
        place(gBtnMax, pageX + (btnW + btnGap) * 2, py, btnW, fanBtnH);
        place(gBtnCustom, pageX + (btnW + btnGap) * 3, py, btnW, fanBtnH);
        py += fanBtnH + uiScale(hwnd, 6);
        place(gFanSlider, pageX, py, std::max(uiScale(hwnd, 80), pageW - uiScale(hwnd, 56)), uiScale(hwnd, 28));
        place(gFanValue, pageX + pageW - uiScale(hwnd, 48), py + uiScale(hwnd, 2), uiScale(hwnd, 48), uiScale(hwnd, 24));
        py += uiScale(hwnd, 34);

        place(gKbTitle, pageX, py, uiScale(hwnd, 100), uiScale(hwnd, 22));
        py += uiScale(hwnd, 22);
        place(gKbSlider, pageX, py, std::max(uiScale(hwnd, 80), pageW - uiScale(hwnd, 56)), uiScale(hwnd, 28));
        place(gKbValue, pageX + pageW - uiScale(hwnd, 48), py + uiScale(hwnd, 2), uiScale(hwnd, 48), uiScale(hwnd, 24));
        py += uiScale(hwnd, 34);

        place(gChargeTitle, pageX, py, uiScale(hwnd, 120), uiScale(hwnd, 22));
        py += uiScale(hwnd, 22);
        // Slider + percent + action buttons on one row to save vertical space.
        const int chargeBtnW = uiScale(hwnd, 120);
        const int chargeBtnH = uiScale(hwnd, 34);
        const int valueW = uiScale(hwnd, 48);
        const int sliderW = std::max(uiScale(hwnd, 120),
                                     pageW - chargeBtnW * 2 - valueW - uiScale(hwnd, 24));
        place(gChargeSlider, pageX, py + uiScale(hwnd, 2), sliderW, uiScale(hwnd, 28));
        place(gChargeValue, pageX + sliderW + uiScale(hwnd, 6), py + uiScale(hwnd, 4), valueW, uiScale(hwnd, 24));
        place(gChargeApply, pageX + sliderW + valueW + uiScale(hwnd, 12), py, chargeBtnW, chargeBtnH);
        place(gChargeReset, pageX + sliderW + valueW + chargeBtnW + uiScale(hwnd, 18), py, chargeBtnW, chargeBtnH);
        SetWindowPos(gChargeApply, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(gChargeReset, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        py += chargeBtnH + uiScale(hwnd, 8);
        const int statusLineH = uiScale(hwnd, 20);
        place(gChargeStatus, pageX, py, pageW, statusLineH);
        gControlPanel.bottom = std::min(contentBottom, py + statusLineH + uiScale(hwnd, 12));
    } else {
        int py = pageY;
        place(gDiagSection, pageX, py, uiScale(hwnd, 160), uiScale(hwnd, 24));
        py += uiScale(hwnd, 28);
        const int gap = uiScale(hwnd, 8);
        const int cols = pageW >= uiScale(hwnd, 760) ? 5 : (pageW >= uiScale(hwnd, 560) ? 4 : 3);
        const int btnW = (pageW - gap * (cols - 1)) / cols;
        const int btnH = uiScale(hwnd, 36);
        HWND buttons[] = {gBtnBattery, gBtnUsb, gBtnEcStatus, gBtnDevice, gBtnFirmware,
                          gBtnSensors, gBtnProtocol, gBackupButton, gReportButton, gBtnCopy};
        for (int i = 0; i < 10; ++i) {
            const int col = i % cols;
            const int row = i / cols;
            place(buttons[i], pageX + col * (btnW + gap), py + row * (btnH + gap), btnW, btnH);
        }
        const int rows = (10 + cols - 1) / cols;
        py += rows * (btnH + gap) + uiScale(hwnd, 8);
        place(gDiagTitle, pageX, py, uiScale(hwnd, 160), uiScale(hwnd, 22));
        py += uiScale(hwnd, 26);
        place(gOutput, pageX, py, pageW, std::max(uiScale(hwnd, 80), contentBottom - py - uiScale(hwnd, 8)));
        gDiagPanel.bottom = contentBottom;
        gControlPanel.bottom = contentBottom;
    }

    SetWindowPos(gPath, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

std::wstring formatPower(double watts) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(1) << watts << L" W";
    return text.str();
}

std::wstring formatDuration(int minutes) {
    if (minutes < 0) return L"--";
    if (minutes < 60) return std::to_wstring(minutes) + L" 分钟";
    return std::to_wstring(minutes / 60) + L" 小时 " + std::to_wstring(minutes % 60) + L" 分钟";
}

void applySnapshot(Snapshot* s) {
    gLastEcConnected = s->connected;
    gLastMaxTemp = s->maxTemp;
    gLastRpms = s->rpms;
    gStatusColor = s->connected ? kGreen : kDanger;
    gTempColor = temperatureColor(s->maxTemp);
    setText(gStatus, s->connected ? L"EC 已连接" : (s->status.empty() ? L"EC 未连接" : s->status));
    setText(gTemp, s->maxTemp >= 0 ? std::to_wstring(s->maxTemp) + L" °C" : L"N/A");
    const int fanCardW = std::max(0, static_cast<int>(gCardFans.right - gCardFans.left));
    setText(gFans, formatFanText(s->rpms, fanCardW > 0 ? fanCardW : 240));
    InvalidateRect(gWindow, nullptr, FALSE);

    if (s->batteryHealth >= 0) {
        setText(gBatteryHealth, L"电池健康：" + std::to_wstring(s->batteryHealth) + L"% · " +
                                    std::to_wstring(s->cycleCount) + L" 次循环");
    } else {
        setText(gBatteryHealth, L"电池健康：N/A");
    }
    if (s->batteryPercent >= 0) {
        setText(gBatteryPower, L"电量：" + std::to_wstring(s->batteryPercent) + L"% · " +
                                   formatPower(s->batteryPowerWatts));
        std::wstring timeText = s->estimatedMinutes >= 0
                                    ? (s->batteryState == L"充电中" ? L"预计充满：" : L"预计可用：") +
                                          formatDuration(s->estimatedMinutes)
                                    : L"预计时间：--";
        setText(gBatteryTime, timeText + L" · " + s->batteryState);
    } else {
        setText(gBatteryPower, L"电量与功耗：N/A");
        setText(gBatteryTime, L"预计时间：N/A");
    }

    if (s->keyboardPercent >= 0) {
        SendMessageW(gKbSlider, TBM_SETPOS, TRUE, s->keyboardPercent);
        setText(gKbValue, std::to_wstring(s->keyboardPercent) + L"%"); EnableWindow(gKbSlider, TRUE);
    } else { setText(gKbValue, L"N/A"); EnableWindow(gKbSlider, FALSE); }

    EnableWindow(gChargeSlider, s->chargeSupported);
    EnableWindow(gChargeApply, s->chargeSupported);
    EnableWindow(gChargeReset, s->chargeSupported);
    if (!s->chargeSupported) {
        setText(gChargeStatus, L"当前 EC 固件不支持 Battery Sustainer 充电上限。 ");
    } else if (s->sustainerEnabled && s->chargeUpper >= 0) {
        if (!gChargeEditing) {
            SendMessageW(gChargeSlider, TBM_SETPOS, TRUE, s->chargeUpper);
            setText(gChargeValue, std::to_wstring(s->chargeUpper) + L"%");
        }
        setText(gChargeStatus, L"已启用：电量维持在 " + std::to_wstring(s->chargeLower) + L"%～" +
                                   std::to_wstring(s->chargeUpper) + L"%（由 EC 固件执行）");
    } else {
        if (!gChargeEditing) {
            SendMessageW(gChargeSlider, TBM_SETPOS, TRUE, 100);
            setText(gChargeValue, L"100%");
        }
        setText(gChargeStatus, L"未限制：正常充电至 100%");
    }
    updateTrayFromSnapshot(*s);
}

bool addTrayIcon(HWND hwnd) {
    if (gTrayIconAdded) return true;

    gTrayIcon = {};
    gTrayIcon.cbSize = sizeof(gTrayIcon);
    gTrayIcon.hWnd = hwnd;
    gTrayIcon.uID = kTrayIconId;
    gTrayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    gTrayIcon.uCallbackMessage = WM_TRAY_ICON;
    gTrayIcon.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
                                                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                    GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wcscpy_s(gTrayIcon.szTip, L"CrosEC Control");
    if (!Shell_NotifyIconW(NIM_ADD, &gTrayIcon)) return false;

    gTrayIconAdded = true;
    gTrayIcon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &gTrayIcon);
    return true;
}

void removeTrayIcon() {
    if (!gTrayIconAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &gTrayIcon);
    gTrayIconAdded = false;
}

void showTrayNotification(const std::wstring& title, const std::wstring& text, DWORD icon) {
    if (!addTrayIcon(gWindow)) return;
    NOTIFYICONDATAW notification = gTrayIcon;
    notification.uFlags = NIF_INFO;
    notification.dwInfoFlags = icon;
    wcsncpy_s(notification.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(notification.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notification);
}

void updateTrayFromSnapshot(const Snapshot& snapshot) {
    if (!addTrayIcon(gWindow)) return;

    std::wstring tooltip = L"CrosEC Control";
    if (snapshot.connected) {
        tooltip += L"\n温度 " + (snapshot.maxTemp >= 0 ? std::to_wstring(snapshot.maxTemp) + L"°C" : L"N/A");
        if (!snapshot.rpms.empty()) {
            tooltip += L" · 风扇 ";
            for (size_t i = 0; i < snapshot.rpms.size(); ++i) {
                if (i) tooltip += L"/";
                tooltip += std::to_wstring(snapshot.rpms[i]);
            }
            tooltip += L" RPM";
        }
        if (snapshot.batteryPercent >= 0)
            tooltip += L"\n电池 " + std::to_wstring(snapshot.batteryPercent) + L"% · " + snapshot.batteryState;
    } else {
        tooltip += L"\nEC 未连接";
    }
    NOTIFYICONDATAW tip = gTrayIcon;
    tip.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(tip.szTip, tooltip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &tip);

    if (!snapshot.connected) {
        if (!gDisconnectNotified) {
            showTrayNotification(L"EC 连接失败", L"CrosEC Control 无法连接到 Embedded Controller。", NIIF_WARNING);
            gDisconnectNotified = true;
        }
    } else {
        if (gHaveSnapshot && !gLastConnected)
            showTrayNotification(L"EC 已重新连接", L"Embedded Controller 状态读取已恢复。", NIIF_INFO);
        gDisconnectNotified = false;

        int alertLevel = snapshot.maxTemp >= 90 ? 2 : snapshot.maxTemp >= 80 ? 1 : 0;
        if (alertLevel > gTemperatureAlertLevel) {
            showTrayNotification(alertLevel == 2 ? L"设备温度过高" : L"设备温度较高",
                                 L"当前最高温度为 " + std::to_wstring(snapshot.maxTemp) + L"°C，请检查散热。",
                                 alertLevel == 2 ? NIIF_ERROR : NIIF_WARNING);
            gTemperatureAlertLevel = alertLevel;
        } else if (snapshot.maxTemp >= 0 && snapshot.maxTemp < 75) {
            gTemperatureAlertLevel = 0;
        }

        bool stoppedFanAtHighTemp = snapshot.maxTemp >= 80 && !snapshot.rpms.empty() &&
                                    std::any_of(snapshot.rpms.begin(), snapshot.rpms.end(),
                                                [](int rpm) { return rpm == 0; });
        if (stoppedFanAtHighTemp && !gFanAlertActive)
            showTrayNotification(L"风扇状态异常", L"高温时至少一个风扇转速为 0 RPM，请立即检查设备。", NIIF_ERROR);
        gFanAlertActive = stoppedFanAtHighTemp;
    }

    gHaveSnapshot = true;
    gLastConnected = snapshot.connected;
}

void showWindowFromTray(HWND hwnd) {
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
}

bool hideWindowToTray(HWND hwnd) {
    if (!addTrayIcon(hwnd)) return false;
    ShowWindow(hwnd, SW_HIDE);

    if (!gTrayHintShown) {
        NOTIFYICONDATAW notification = gTrayIcon;
        notification.uFlags = NIF_INFO;
        notification.dwInfoFlags = NIIF_INFO;
        wcscpy_s(notification.szInfoTitle, L"CrosEC Control 仍在运行");
        wcscpy_s(notification.szInfo, L"双击托盘图标可恢复窗口，右键菜单可退出程序。");
        Shell_NotifyIconW(NIM_MODIFY, &notification);
        gTrayHintShown = true;
    }
    return true;
}

void showTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ID_TRAY_SHOW, L"显示主窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetMenuDefaultItem(menu, ID_TRAY_SHOW, FALSE);

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd);
    UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                  point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command) PostMessageW(hwnd, WM_COMMAND, command, 0);
    PostMessageW(hwnd, WM_NULL, 0, 0);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (gTaskbarCreatedMessage && msg == gTaskbarCreatedMessage) {
        gTrayIconAdded = false;
        addTrayIcon(hwnd);
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        BOOL dark = TRUE; DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        createUi(hwnd);
        refreshEctoolLocation();
        layout(hwnd);
        applySystemMetrics(sampleSystemMetrics());
        SetTimer(hwnd, TIMER_SYSTEM, 1000, nullptr);
        addTrayIcon(hwnd);
        gWorker = std::thread(workerMain);
        if (gEctool.empty())
            PostMessageW(hwnd, WM_ECTOOL_GUIDE, 0, 0);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        const int minW = uiScale(hwnd, kMinClientW);
        const int minH = uiScale(hwnd, kMinClientH);
        RECT pad{0, 0, minW, minH};
        AdjustWindowRectEx(&pad, WS_OVERLAPPEDWINDOW, FALSE, 0);
        info->ptMinTrackSize.x = pad.right - pad.left;
        info->ptMinTrackSize.y = pad.bottom - pad.top;
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd);
        return 0;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            hideWindowToTray(hwnd);
            return 0;
        }
        layout(hwnd); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        paintUi(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (item && item->CtlType == ODT_BUTTON) {
            drawOwnerButton(item);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND ctrl = reinterpret_cast<HWND>(lp);
        COLORREF text = kText;
        if (ctrl == gTemp) text = gTempColor;
        else if (ctrl == gCpu) text = gCpuColor;
        else if (ctrl == gMem) text = gMemColor;
        else if (ctrl == gDisk) text = gDiskColor;
        else if (ctrl == gStatus) text = gStatusColor;
        else if (ctrl == gHeaderSub || ctrl == gTempCaption || ctrl == gFansCaption || ctrl == gModeCaption ||
                 ctrl == gCpuCaption || ctrl == gMemCaption || ctrl == gDiskCaption ||
                 ctrl == gBatteryHealth || ctrl == gBatteryPower || ctrl == gBatteryTime || ctrl == gChargeStatus ||
                 ctrl == gPath) text = kMuted;
        SetTextColor(dc, text);

        if (ctrl == gStatus) {
            HBRUSH brush = gStatusWaitBrush;
            COLORREF bg = RGB(40, 42, 48);
            if (gHaveSnapshot || gLastEcConnected) {
                brush = gLastEcConnected ? gStatusOkBrush : gStatusBadBrush;
                bg = gLastEcConnected ? RGB(24, 48, 38) : RGB(52, 30, 30);
            }
            SetBkColor(dc, bg);
            return reinterpret_cast<LRESULT>(brush);
        }

        UiSurface surface = UiSurface::Background;
        for (const auto& item : gUiItems) {
            if (item.hwnd == ctrl) { surface = item.surface; break; }
        }
        if (surface == UiSurface::Panel) {
            SetBkColor(dc, kPanel);
            // Metric cards and status bar sit on the slightly lighter panel2 fill.
            if (ctrl == gTemp || ctrl == gTempCaption || ctrl == gFans || ctrl == gFansCaption ||
                ctrl == gMode || ctrl == gModeCaption || ctrl == gCpu || ctrl == gCpuCaption ||
                ctrl == gMem || ctrl == gMemCaption || ctrl == gDisk || ctrl == gDiskCaption ||
                ctrl == gPath) {
                SetBkColor(dc, kPanel2);
                return reinterpret_cast<LRESULT>(gPanel2Brush);
            }
            return reinterpret_cast<LRESULT>(gPanelBrush);
        }
        SetBkColor(dc, kBg);
        return reinterpret_cast<LRESULT>(gBgBrush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp); SetTextColor(dc, kText); SetBkColor(dc, RGB(22, 25, 29)); return reinterpret_cast<LRESULT>(gEditBrush);
    }
    case WM_HSCROLL: {
        HWND source = reinterpret_cast<HWND>(lp);
        if (source == gFanSlider) {
            int v = static_cast<int>(SendMessageW(source, TBM_GETPOS, 0, 0));
            setText(gFanValue, std::to_wstring(v) + L"%"); gPendingSlider = 1;
            KillTimer(hwnd, TIMER_SLIDER); SetTimer(hwnd, TIMER_SLIDER, 350, nullptr);
        } else if (source == gKbSlider) {
            int v = static_cast<int>(SendMessageW(source, TBM_GETPOS, 0, 0));
            setText(gKbValue, std::to_wstring(v) + L"%"); gPendingSlider = 2;
            KillTimer(hwnd, TIMER_SLIDER); SetTimer(hwnd, TIMER_SLIDER, 350, nullptr);
        } else if (source == gChargeSlider) {
            int v = static_cast<int>(SendMessageW(source, TBM_GETPOS, 0, 0));
            gChargeEditing = true;
            setText(gChargeValue, std::to_wstring(v) + L"%");
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_SYSTEM) {
            applySystemMetrics(sampleSystemMetrics());
            return 0;
        }
        if (wp == TIMER_SLIDER) {
            KillTimer(hwnd, TIMER_SLIDER);
            if (gPendingSlider == 1) {
                int v = static_cast<int>(SendMessageW(gFanSlider, TBM_GETPOS, 0, 0));
                commandResult(L"自定义风扇", {L"fanduty", std::to_wstring(v)}, L"自定义");
            } else if (gPendingSlider == 2) {
                int v = static_cast<int>(SendMessageW(gKbSlider, TBM_GETPOS, 0, 0));
                runAsync(L"键盘背光", {L"pwmsetkblight", std::to_wstring(v)});
            }
            gPendingSlider = 0;
        } return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TAB_CONTROL: setActiveTab(0); break;
        case ID_TAB_DIAG: setActiveTab(1); break;
        case ID_REFRESH: {
            const bool found = refreshEctoolLocation();
            if (!found) {
                setText(gStatus, L"未找到 ECTOOL");
                InvalidateRect(gStatus, nullptr, TRUE);
                if (MessageBoxW(hwnd,
                                L"仍未找到 ectool.exe。\n\n是否打开下载链接引导？\n"
                                L"（含 GitHub 直链与代理链接，由浏览器下载安装）",
                                L"未找到 ectool",
                                MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON1) == IDYES) {
                    showEctoolInstallGuide(hwnd);
                    refreshEctoolLocation();
                }
                break;
            }
            gRefreshNow = true;
            setText(gStatus, L"正在刷新…");
            InvalidateRect(gStatus, nullptr, TRUE);
            break;
        }
        case ID_AUTO: commandResult(L"风扇自动控制", {L"autofanctrl"}, L"自动"); break;
        case ID_OFF:
            if (MessageBoxW(hwnd, L"关闭风扇可能导致设备过热。\n\n确定要将风扇占空比设为 0% 吗？", L"安全警告", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES)
                commandResult(L"关闭风扇", {L"fanduty", L"0"}, L"关闭");
            break;
        case ID_MAX: commandResult(L"最大风扇", {L"fanduty", L"100"}, L"最大"); break;
        case ID_CUSTOM: {
            int v = static_cast<int>(SendMessageW(gFanSlider, TBM_GETPOS, 0, 0));
            commandResult(L"自定义风扇", {L"fanduty", std::to_wstring(v)}, L"自定义"); break;
        }
        case ID_CHARGE_APPLY: {
            int upper = static_cast<int>(SendMessageW(gChargeSlider, TBM_GETPOS, 0, 0));
            if (upper >= 100) {
                gChargeEditing = false;
                runAsync(L"恢复完整充电", {L"chargecontrol", L"normal"});
            } else {
                int lower = std::max(0, upper - 5);
                std::wstring prompt = L"EC 将把电池电量维持在 " + std::to_wstring(lower) + L"%～" +
                                      std::to_wstring(upper) + L"%。\n\n该设置由 EC 固件保存和执行，确定应用吗？";
                if (MessageBoxW(hwnd, prompt.c_str(), L"设置最大充电上限", MB_ICONINFORMATION | MB_YESNO) == IDYES) {
                    gChargeEditing = false;
                    runAsync(L"设置充电上限", {L"chargecontrol", L"normal", std::to_wstring(lower), std::to_wstring(upper)});
                }
            }
            break;
        }
        case ID_CHARGE_RESET:
            if (MessageBoxW(hwnd, L"确定关闭 Battery Sustainer 并恢复正常充电至 100% 吗？", L"恢复完整充电", MB_ICONINFORMATION | MB_YESNO) == IDYES) {
                gChargeEditing = false;
                runAsync(L"恢复完整充电", {L"chargecontrol", L"normal"});
            }
            break;
        case ID_DRIVER_BACKUP: {
            if (MessageBoxW(hwnd,
                            L"将使用 Windows PnPUtil 只读导出当前 Driver Store 中的第三方驱动，"
                            L"并复制 CROS-EC 配套工具。\n\n"
                            L"此操作不会安装、删除或替换任何驱动，但需要管理员权限。是否继续？",
                            L"备份 Windows 驱动", MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
                break;
            std::wstring folder = chooseFolder(hwnd);
            if (!folder.empty()) startDriverBackup(folder);
            break;
        }
        case ID_BATTERY: runAsync(L"电池详情", {L"battery"}); break;
        case ID_VERSION: runAsync(L"EC 版本", {L"version"}); break;
        case ID_CHIP: runAsync(L"EC 芯片信息", {L"chipinfo"}); break;
        case ID_PROTOCOL: runAsync(L"EC 协议信息", {L"protoinfo"}); break;
        case ID_SENSORS:
            runDiagnosticGroup(L"传感器与温控", {
                {L"当前温度", {L"temps", L"all"}},
                {L"传感器信息", {L"tempsinfo", L"all"}},
                {L"热管理阈值", {L"thermalget"}},
                {L"动作传感器", {L"motionsense"}}
            });
            break;
        case ID_USB_C:
            runDiagnosticGroup(L"USB-C / PD", {
                {L"端口 0 电源", {L"usbpdpower", L"0"}},
                {L"端口 1 电源", {L"usbpdpower", L"1"}},
                {L"端口 0 状态", {L"typecstatus", L"0"}},
                {L"端口 1 状态", {L"typecstatus", L"1"}},
                {L"USB-C MUX", {L"usbpdmuxinfo"}}
            });
            break;
        case ID_EC_STATUS:
            runDiagnosticGroup(L"EC 运行状态", {
                {L"系统信息", {L"sysinfo"}},
                {L"运行时间与重启原因", {L"uptimeinfo"}},
                {L"Panic 信息", {L"panicinfo"}},
                {L"Port 80 启动记录", {L"port80read"}}
            });
            break;
        case ID_DEVICE_STATUS:
            runDiagnosticGroup(L"设备模式", {
                {L"物理开关", {L"switches"}},
                {L"设备开关与平板模式", {L"mkbpget", L"switches"}},
                {L"屏幕转轴角度", {L"motionsense", L"lid_angle"}},
                {L"键盘矩阵", {L"kbinfo"}}
            });
            break;
        case ID_FIRMWARE_INFO:
            runDiagnosticGroup(L"固件与硬件", {
                {L"EC 版本", {L"version"}},
                {L"支持能力", {L"inventory"}},
                {L"主板版本", {L"boardversion"}},
                {L"芯片", {L"chipinfo"}},
                {L"Flash 信息", {L"flashinfo"}}
            });
            break;
        case ID_DIAGNOSTIC_REPORT: {
            std::filesystem::path path = chooseReportFile(hwnd);
            if (!path.empty()) startDiagnosticReport(path);
            break;
        }
        case ID_COPY: {
            int n = GetWindowTextLengthW(gOutput);
            std::wstring text(static_cast<size_t>(n) + 1, L'\0');
            GetWindowTextW(gOutput, text.data(), n + 1);
            text.resize(static_cast<size_t>(n));
            if (OpenClipboard(hwnd)) { EmptyClipboard(); SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t); HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes); if (mem) { memcpy(GlobalLock(mem), text.c_str(), bytes); GlobalUnlock(mem); SetClipboardData(CF_UNICODETEXT, mem); } CloseClipboard(); }
            break;
        }
        case ID_TRAY_SHOW: showWindowFromTray(hwnd); break;
        case ID_TRAY_EXIT:
            gExitRequested = true;
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        } return 0;
    case WM_TRAY_ICON:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONDBLCLK:
            showWindowFromTray(hwnd);
            break;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            showTrayMenu(hwnd);
            break;
        }
        return 0;
    case WM_EC_UPDATE: {
        auto* s = reinterpret_cast<Snapshot*>(lp); applySnapshot(s); delete s; return 0;
    }
    case WM_EC_RESULT: {
        auto* r = reinterpret_cast<AsyncResult*>(lp); setOutput(r->error ? r->title + L" · 失败" : r->title, r->text); delete r; return 0;
    }
    case WM_DRIVER_BACKUP: {
        auto* result = reinterpret_cast<BackupResult*>(lp);
        EnableWindow(gBackupButton, TRUE);
        setOutput(result->success ? L"驱动备份完成" : L"驱动备份失败", result->message);
        if (result->success && !result->folder.empty() &&
            MessageBoxW(hwnd, L"驱动备份完成。是否立即打开备份目录？", L"CrosEC Control", MB_ICONINFORMATION | MB_YESNO) == IDYES)
            ShellExecuteW(hwnd, L"open", result->folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        delete result; return 0;
    }
    case WM_DIAGNOSTIC_REPORT: {
        auto* result = reinterpret_cast<ReportResult*>(lp);
        EnableWindow(gReportButton, TRUE);
        setOutput(result->success ? L"诊断报告已保存" : L"诊断报告失败", result->message);
        if (result->success && !result->path.empty() &&
            MessageBoxW(hwnd, L"诊断报告已保存。是否立即打开？", L"CrosEC Control", MB_ICONINFORMATION | MB_YESNO) == IDYES)
            ShellExecuteW(hwnd, L"open", result->path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        delete result; return 0;
    }
    case WM_ECTOOL_GUIDE:
        if (gEctool.empty()) {
            showEctoolInstallGuide(hwnd);
            refreshEctoolLocation();
            if (!gEctool.empty()) {
                gRefreshNow = true;
                setText(gStatus, L"正在刷新…");
                InvalidateRect(gStatus, nullptr, TRUE);
            }
        }
        return 0;
    case WM_CLOSE:
        if (!gExitRequested && hideWindowToTray(hwnd)) return 0;
        if (gManualFan) runEctool({L"autofanctrl"});
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_SYSTEM);
        KillTimer(hwnd, TIMER_SLIDER);
        removeTrayIcon();
        gStop = true; gRefreshNow = true; if (gWorker.joinable()) gWorker.join();
        DeleteObject(gFont); DeleteObject(gFontSmall); DeleteObject(gFontLarge); DeleteObject(gFontTitle);
        DeleteObject(gFontStatus); DeleteObject(gFontMono);
        DeleteObject(gBgBrush); DeleteObject(gPanelBrush); DeleteObject(gPanel2Brush); DeleteObject(gEditBrush);
        DeleteObject(gStatusOkBrush); DeleteObject(gStatusBadBrush); DeleteObject(gStatusWaitBrush);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    gTaskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{sizeof(wc)}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = wndProc; wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hbrBackground = CreateSolidBrush(kBg); wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // Prefer a laptop-friendly default in physical pixels (Per-Monitor V2).
    const UINT dpi = GetDpiForSystem();
    auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96); };
    int startW = scale(920);
    int startH = scale(780);
    RECT work{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const int workW = work.right - work.left;
        const int workH = work.bottom - work.top;
        startW = std::min(startW, std::max(scale(kMinClientW + 32), workW - scale(40)));
        startH = std::min(startH, std::max(scale(kMinClientH + 40), workH - scale(40)));
    }
    HWND hwnd = CreateWindowExW(0, kClassName, L"CrosEC Control", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, startW, startH, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(msg.wParam);
}
