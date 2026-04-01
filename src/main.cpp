#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "telemetry.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 480;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT_PTR kMoveTimerId = 2;
constexpr UINT kRefreshTimerMs = 250;
constexpr UINT kMoveTimerMs = 16;
constexpr COLORREF kBlack = RGB(0, 0, 0);
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kAccent = RGB(0, 191, 255);
constexpr COLORREF kPanelBorder = RGB(235, 235, 235);
constexpr COLORREF kMuted = RGB(165, 180, 190);
constexpr COLORREF kTrack = RGB(45, 52, 58);
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kCommandMove = 1001;
constexpr UINT kCommandBringOnTop = 1002;
constexpr UINT kCommandUpdateConfig = 1003;
constexpr UINT kCommandExit = 1004;
constexpr wchar_t kWindowClassName[] = L"SystemTelemetryDashboard";

std::wstring Trim(const std::wstring& input) {
    const auto first = input.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = input.find_last_not_of(L" \t\r\n");
    return input.substr(first, last - first + 1);
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool ContainsInsensitive(const std::wstring& value, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::wstring::npos;
}

std::wstring FormatValue(const ScalarMetric& metric, int precision = 1) {
    if (!metric.value.has_value()) {
        return L"N/A";
    }
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.*f %ls", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::wstring FormatMemory(double usedGb, double totalGb) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.1f / %.0f GB", usedGb, totalGb);
    return buffer;
}

std::wstring FormatDriveFree(double freeGb) {
    wchar_t buffer[64];
    if (freeGb >= 1024.0) {
        swprintf_s(buffer, L"%.1f TB free", freeGb / 1024.0);
    } else {
        swprintf_s(buffer, L"%.0f GB free", freeGb);
    }
    return buffer;
}

std::filesystem::path GetRuntimeConfigPath();

std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(modulePath).parent_path();
}

bool WriteUtf8File(const std::filesystem::path& path, const std::wstring& text) {
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return false;
    }

    std::string bytes(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data(), required, nullptr, nullptr);

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    bool ok = WriteFile(file, bom, sizeof(bom), &written, nullptr) == TRUE && written == sizeof(bom);
    if (ok) {
        ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) == TRUE &&
            written == bytes.size();
    }
    CloseHandle(file);
    return ok;
}

bool HasSwitch(const std::wstring& target) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return false;
    }

    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], target.c_str()) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

int RunDumpMode() {
    TelemetryCollector telemetry;
    const AppConfig config = LoadConfig(GetRuntimeConfigPath());
    if (!telemetry.Initialize(config)) {
        MessageBoxW(nullptr, L"Failed to initialize telemetry collector.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }

    Sleep(900);
    telemetry.UpdateSnapshot();
    Sleep(1100);
    telemetry.UpdateSnapshot();
    const std::filesystem::path dumpPath = GetExecutableDirectory() / L"telemetry_dump.txt";
    if (!WriteUtf8File(dumpPath, telemetry.DumpText())) {
        const std::wstring message = L"Failed to write dump file:\n" + dumpPath.wstring();
        MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return 1;
    }
    return 0;
}

std::wstring FormatSpeed(double mbps) {
    wchar_t buffer[64];
    if (mbps >= 100.0) {
        swprintf_s(buffer, L"%.0f MB/s", mbps);
    } else {
        swprintf_s(buffer, L"%.1f MB/s", mbps);
    }
    return buffer;
}

struct MonitorPlacementInfo {
    std::wstring deviceName;
    std::wstring monitorName = L"Unknown";
    std::wstring configMonitorName = L"";
    RECT monitorRect{};
    POINT relativePosition{};
};

struct MonitorIdentity {
    std::wstring displayName;
    std::wstring configName;
};

std::wstring SimplifyDeviceName(const std::wstring& deviceName) {
    if (deviceName.rfind(L"\\\\.\\", 0) == 0) {
        return deviceName.substr(4);
    }
    return deviceName;
}

bool IsUsefulFriendlyName(const std::wstring& name) {
    const std::wstring lowered = ToLower(name);
    return !name.empty() &&
        lowered != L"generic pnp monitor" &&
        lowered.find(L"\\\\?\\display") != 0;
}

MonitorIdentity GetMonitorIdentity(const std::wstring& deviceName);

std::optional<RECT> FindTargetMonitor(const std::wstring& requestedName) {
    if (requestedName.empty()) {
        return std::nullopt;
    }
    struct SearchContext {
        std::wstring requestedName;
        std::optional<RECT> result;
    } context{requestedName, std::nullopt};

    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto* context = reinterpret_cast<SearchContext*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) {
                return TRUE;
            }

            const MonitorIdentity identity = GetMonitorIdentity(info.szDevice);
            if (ContainsInsensitive(identity.displayName, context->requestedName) ||
                ContainsInsensitive(identity.configName, context->requestedName) ||
                ContainsInsensitive(info.szDevice, context->requestedName)) {
                context->result = info.rcMonitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    return context.result;
}

MonitorIdentity GetMonitorIdentity(const std::wstring& deviceName) {
    MonitorIdentity identity;
    identity.displayName = SimplifyDeviceName(deviceName);
    identity.configName = deviceName;

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
        return identity;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return identity;
    }

    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        sourceName.header.size = sizeof(sourceName);
        sourceName.header.adapterId = paths[i].sourceInfo.adapterId;
        sourceName.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS) {
            continue;
        }

        if (_wcsicmp(sourceName.viewGdiDeviceName, deviceName.c_str()) != 0) {
            continue;
        }

        DISPLAYCONFIG_TARGET_DEVICE_NAME targetName{};
        targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        targetName.header.size = sizeof(targetName);
        targetName.header.adapterId = paths[i].targetInfo.adapterId;
        targetName.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&targetName.header) != ERROR_SUCCESS) {
            continue;
        }

        const std::wstring friendlyName = targetName.monitorFriendlyDeviceName;
        const std::wstring monitorPath = targetName.monitorDevicePath;
        if (IsUsefulFriendlyName(friendlyName)) {
            identity.displayName = friendlyName + L" (" + SimplifyDeviceName(deviceName) + L")";
            identity.configName = friendlyName;
        } else if (!monitorPath.empty()) {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = monitorPath;
        } else {
            identity.displayName = SimplifyDeviceName(deviceName);
            identity.configName = deviceName;
        }
        return identity;
    }

    return identity;
}

MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd) {
    MonitorPlacementInfo info;
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        info.deviceName = monitorInfo.szDevice;
        const MonitorIdentity identity = GetMonitorIdentity(monitorInfo.szDevice);
        info.monitorName = identity.displayName;
        info.configMonitorName = identity.configName;
        info.monitorRect = monitorInfo.rcMonitor;
        info.relativePosition.x = windowRect.left - monitorInfo.rcMonitor.left;
        info.relativePosition.y = windowRect.top - monitorInfo.rcMonitor.top;
    }
    return info;
}

HFONT CreateUiFont(int height, int weight, const wchar_t* face) {
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, face);
}

void ShutdownPreviousInstance() {
    HWND existing = FindWindowW(kWindowClassName, nullptr);
    if (existing == nullptr) {
        return;
    }

    const DWORD existingProcessId = [&]() {
        DWORD processId = 0;
        GetWindowThreadProcessId(existing, &processId);
        return processId;
    }();

    if (existingProcessId == GetCurrentProcessId()) {
        return;
    }

    PostMessageW(existing, WM_CLOSE, 0, 0);
    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(100);
        existing = FindWindowW(kWindowClassName, nullptr);
        if (existing == nullptr) {
            return;
        }
    }
}

std::filesystem::path GetRuntimeConfigPath() {
    return std::filesystem::current_path() / L"config.ini";
}

class DashboardApp {
public:
    bool Initialize(HINSTANCE instance);
    int Run();

private:
    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void ShowContextMenu(POINT screenPoint);
    void BringOnTop();
    void UpdateConfigFromCurrentPlacement();
    void StartMoveMode();
    void StopMoveMode();
    void UpdateMoveTracking();
    void DrawMoveOverlay(HDC hdc);
    bool CreateTrayIcon();
    void RemoveTrayIcon();

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::wstring& text, HFONT font,
        COLORREF color, UINT format);
    void DrawPanel(HDC hdc, const RECT& rect, const std::wstring& title);
    POINT PolarPoint(int cx, int cy, int radius, double angleDegrees);
    void DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::wstring& label);
    void DrawMetricRow(HDC hdc, const RECT& rect, const std::wstring& label, const std::wstring& value, double ratio);
    void DrawProcessorPanel(HDC hdc, const RECT& rect, const ProcessorTelemetry& cpu);
    void DrawGpuPanel(HDC hdc, const RECT& rect, const GpuTelemetry& gpu);
    void DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue);
    void DrawNetworkPanel(HDC hdc, const RECT& rect, const NetworkTelemetry& network);
    void DrawStoragePanel(HDC hdc, const RECT& rect, const std::vector<DriveInfo>& drives);
    void DrawTimePanel(HDC hdc, const RECT& rect, const SYSTEMTIME& now);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);

    struct Fonts {
        HFONT title = nullptr;
        HFONT big = nullptr;
        HFONT value = nullptr;
        HFONT label = nullptr;
        HFONT smallFont = nullptr;
    } fonts_;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig config_;
    TelemetryCollector telemetry_;
    bool isMoving_ = false;
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
};

bool DashboardApp::Initialize(HINSTANCE instance) {
    instance_ = instance;
    config_ = LoadConfig(GetRuntimeConfigPath());
    telemetry_.Initialize(config_);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBlack);
    if (!RegisterClassW(&wc)) {
        return false;
    }

    RECT placement{100, 100, 100 + kWindowWidth, 100 + kWindowHeight};
    if (const auto monitor = FindTargetMonitor(config_.monitorName); monitor.has_value()) {
        placement.left = monitor->left + config_.positionX;
        placement.top = monitor->top + config_.positionY;
    } else {
        placement.left = 100 + config_.positionX;
        placement.top = 100 + config_.positionY;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"System Telemetry",
        WS_POPUP,
        placement.left,
        placement.top,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        this);
    return hwnd_ != nullptr;
}

void DashboardApp::BringOnTop() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
}

void DashboardApp::UpdateConfigFromCurrentPlacement() {
    const MonitorPlacementInfo placement = GetMonitorPlacementForWindow(hwnd_);
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    const std::wstring monitorName = !placement.configMonitorName.empty()
        ? placement.configMonitorName
        : placement.deviceName;
    if (!SaveDisplayConfig(configPath, monitorName, placement.relativePosition.x, placement.relativePosition.y)) {
        const std::wstring message = L"Failed to update " + configPath.wstring() + L".";
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return;
    }

    config_.monitorName = monitorName;
    config_.positionX = placement.relativePosition.x;
    config_.positionY = placement.relativePosition.y;
}

bool DashboardApp::CreateTrayIcon() {
    trayIcon_ = {};
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"System Telemetry");
    return Shell_NotifyIconW(NIM_ADD, &trayIcon_) == TRUE;
}

void DashboardApp::RemoveTrayIcon() {
    if (trayIcon_.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
    }
}

void DashboardApp::StartMoveMode() {
    isMoving_ = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StopMoveMode() {
    if (!isMoving_) {
        return;
    }
    isMoving_ = false;
    KillTimer(hwnd_, kMoveTimerId);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::UpdateMoveTracking() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }

    const int x = cursor.x - (kWindowWidth / 2);
    const int y = cursor.y - 24;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
}

void DashboardApp::ShowContextMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandUpdateConfig, L"Update Config");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
    SetForegroundWindow(hwnd_);
    const UINT selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (selected) {
    case kCommandMove:
        StartMoveMode();
        break;
    case kCommandBringOnTop:
        BringOnTop();
        break;
    case kCommandUpdateConfig:
        UpdateConfigFromCurrentPlacement();
        break;
    case kCommandExit:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

void DashboardApp::DrawMoveOverlay(HDC hdc) {
    RECT overlay{16, 16, 420, 112};
    HBRUSH fill = CreateSolidBrush(RGB(0, 0, 0));
    HPEN border = CreatePen(PS_SOLID, 1, kAccent);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, border);
    RoundRect(hdc, overlay.left, overlay.top, overlay.right, overlay.bottom, 14, 14);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(fill);
    DeleteObject(border);

    RECT titleRect{overlay.left + 12, overlay.top + 8, overlay.right - 12, overlay.top + 28};
    RECT monitorRect{overlay.left + 12, overlay.top + 34, overlay.right - 12, overlay.top + 56};
    RECT positionRect{overlay.left + 12, overlay.top + 58, overlay.right - 12, overlay.top + 80};
    RECT hintRect{overlay.left + 12, overlay.top + 82, overlay.right - 12, overlay.bottom - 12};

    wchar_t positionText[96];
    swprintf_s(positionText, L"Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);

    DrawTextBlock(hdc, titleRect, L"Move Mode", fonts_.label, kAccent, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, L"Monitor: " + movePlacementInfo_.monitorName, fonts_.smallFont, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, fonts_.smallFont, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, L"Left-click to place. Copy monitor name and x/y into config.", fonts_.smallFont,
        kMuted, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

int DashboardApp::Run() {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK DashboardApp::WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<DashboardApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DashboardApp::WndProcThunk));
        app->hwnd_ = hwnd;
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK DashboardApp::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app != nullptr ? app->HandleMessage(message, wParam, lParam)
                          : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        fonts_.title = CreateUiFont(18, FW_BOLD, L"Segoe UI Semibold");
        fonts_.big = CreateUiFont(40, FW_BOLD, L"Segoe UI Semibold");
        fonts_.value = CreateUiFont(17, FW_BOLD, L"Segoe UI Semibold");
        fonts_.label = CreateUiFont(14, FW_NORMAL, L"Segoe UI");
        fonts_.smallFont = CreateUiFont(12, FW_NORMAL, L"Segoe UI");
        SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
        CreateTrayIcon();
        return 0;
    case WM_TIMER:
        if (wParam == kMoveTimerId) {
            UpdateMoveTracking();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        telemetry_.UpdateSnapshot();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            RECT rect{};
            GetWindowRect(hwnd_, &rect);
            point.x = rect.left + 24;
            point.y = rect.top + 24;
        }
        if (isMoving_) {
            StopMoveMode();
        }
        ShowContextMenu(point);
        return 0;
    }
    case WM_LBUTTONUP:
        if (isMoving_) {
            StopMoveMode();
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && isMoving_) {
            StopMoveMode();
            return 0;
        }
        break;
    case kTrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowContextMenu(point);
            return 0;
        }
        break;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, kRefreshTimerId);
        KillTimer(hwnd_, kMoveTimerId);
        RemoveTrayIcon();
        DeleteObject(fonts_.title);
        DeleteObject(fonts_.big);
        DeleteObject(fonts_.value);
        DeleteObject(fonts_.label);
        DeleteObject(fonts_.smallFont);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DashboardApp::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);

    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    HBRUSH background = CreateSolidBrush(kBlack);
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);

    DrawLayout(memDc, telemetry_.Snapshot());
    if (isMoving_) {
        DrawMoveOverlay(memDc);
    }

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd_, &ps);
}

void DashboardApp::DrawTextBlock(HDC hdc, const RECT& rect, const std::wstring& text, HFONT font,
    COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    DrawTextW(hdc, text.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
}

void DashboardApp::DrawPanel(HDC hdc, const RECT& rect, const std::wstring& title) {
    HPEN border = CreatePen(PS_SOLID, 1, kPanelBorder);
    HBRUSH fill = CreateSolidBrush(RGB(6, 8, 11));
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 18, 18);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(border);

    RECT titleRect = rect;
    titleRect.left += 14;
    titleRect.top += 8;
    titleRect.right -= 14;
    titleRect.bottom = titleRect.top + 24;
    DrawTextBlock(hdc, titleRect, title, fonts_.title, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

POINT DashboardApp::PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{
        cx + static_cast<LONG>(std::round(std::cos(radians) * radius)),
        cy - static_cast<LONG>(std::round(std::sin(radians) * radius))
    };
}

void DashboardApp::DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::wstring& label) {
    HPEN trackPen = CreatePen(PS_SOLID, 10, kTrack);
    HPEN accentPen = CreatePen(PS_SOLID, 10, kAccent);
    HGDIOBJ oldPen = SelectObject(hdc, trackPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const RECT bounds{cx - radius, cy - radius, cx + radius, cy + radius};
    const POINT startTrack = PolarPoint(cx, cy, radius, 135.0);
    const POINT endTrack = PolarPoint(cx, cy, radius, -135.0);
    Arc(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom,
        startTrack.x, startTrack.y, endTrack.x, endTrack.y);

    SelectObject(hdc, accentPen);
    const double sweep = 270.0 * std::clamp(percent, 0.0, 100.0) / 100.0;
    const POINT endValue = PolarPoint(cx, cy, radius, 135.0 - sweep);
    Arc(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom,
        startTrack.x, startTrack.y, endValue.x, endValue.y);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(trackPen);
    DeleteObject(accentPen);

    RECT numberRect{cx - 42, cy - 28, cx + 42, cy + 18};
    wchar_t number[16];
    swprintf_s(number, L"%.0f%%", percent);
    DrawTextBlock(hdc, numberRect, number, fonts_.big, kWhite, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect{cx - 42, cy + 18, cx + 42, cy + 42};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, kMuted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DashboardApp::DrawMetricRow(
    HDC hdc, const RECT& rect, const std::wstring& label, const std::wstring& value, double ratio) {
    RECT labelRect{rect.left, rect.top, rect.left + 74, rect.bottom};
    RECT valueRect{rect.left + 82, rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, label, fonts_.label, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, valueRect, value, fonts_.value, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT barRect{rect.left + 82, rect.bottom - 5, rect.right, rect.bottom - 1};
    HBRUSH track = CreateSolidBrush(kTrack);
    FillRect(hdc, &barRect, track);
    DeleteObject(track);

    RECT fill = barRect;
    fill.right = fill.left + static_cast<int>((fill.right - fill.left) * std::clamp(ratio, 0.0, 1.0));
    HBRUSH accent = CreateSolidBrush(kAccent);
    FillRect(hdc, &fill, accent);
    DeleteObject(accent);
}

void DashboardApp::DrawProcessorPanel(HDC hdc, const RECT& rect, const ProcessorTelemetry& cpu) {
    DrawPanel(hdc, rect, L"CPU");
    RECT nameRect{rect.left + 16, rect.top + 34, rect.right - 16, rect.top + 58};
    DrawTextBlock(hdc, nameRect, cpu.name, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGauge(hdc, rect.left + 92, rect.top + 132, 52, cpu.loadPercent, L"Load");

    int y = rect.top + 76;
    const int rowHeight = 34;
    RECT rows{rect.left + 164, y, rect.right - 18, y + rowHeight};
    DrawMetricRow(hdc, rows, L"Temp", FormatValue(cpu.temperature, 0), cpu.temperature.value.value_or(0.0) / 100.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Power", FormatValue(cpu.power, 1), cpu.power.value.value_or(0.0) / 150.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Clock", FormatValue(cpu.clock, 2), cpu.clock.value.value_or(0.0) / 5.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Fan", FormatValue(cpu.fan, 0), cpu.fan.value.value_or(0.0) / 4000.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"RAM", FormatMemory(cpu.memory.usedGb, cpu.memory.totalGb),
        cpu.memory.totalGb > 0.0 ? cpu.memory.usedGb / cpu.memory.totalGb : 0.0);
}

void DashboardApp::DrawGpuPanel(HDC hdc, const RECT& rect, const GpuTelemetry& gpu) {
    DrawPanel(hdc, rect, L"GPU");
    RECT nameRect{rect.left + 16, rect.top + 34, rect.right - 16, rect.top + 58};
    DrawTextBlock(hdc, nameRect, gpu.name, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGauge(hdc, rect.left + 92, rect.top + 132, 52, gpu.loadPercent, L"Load");

    int y = rect.top + 76;
    const int rowHeight = 34;
    RECT rows{rect.left + 164, y, rect.right - 18, y + rowHeight};
    DrawMetricRow(hdc, rows, L"Temp", FormatValue(gpu.temperature, 0), gpu.temperature.value.value_or(0.0) / 100.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Power", FormatValue(gpu.power, 1), gpu.power.value.value_or(0.0) / 350.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Clock", FormatValue(gpu.clock, 0), gpu.clock.value.value_or(0.0) / 2600.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"Fan", FormatValue(gpu.fan, 0), gpu.fan.value.value_or(0.0) / 3000.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, L"VRAM", FormatMemory(gpu.vram.usedGb, std::max(1.0, gpu.vram.totalGb)),
        gpu.vram.totalGb > 0.0 ? gpu.vram.usedGb / gpu.vram.totalGb : 0.0);
}

void DashboardApp::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue) {
    HBRUSH bg = CreateSolidBrush(RGB(10, 12, 15));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 2, kAccent);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    const int width = std::max<int>(1, rect.right - rect.left - 1);
    const int height = std::max<int>(1, rect.bottom - rect.top - 1);
    for (size_t i = 1; i < history.size(); ++i) {
        const double v1 = std::clamp(history[i - 1] / maxValue, 0.0, 1.0);
        const double v2 = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x1 = rect.left + static_cast<int>((i - 1) * width / std::max<size_t>(1, history.size() - 1));
        const int x2 = rect.left + static_cast<int>(i * width / std::max<size_t>(1, history.size() - 1));
        const int y1 = rect.bottom - 1 - static_cast<int>(v1 * height);
        const int y2 = rect.bottom - 1 - static_cast<int>(v2 * height);
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardApp::DrawNetworkPanel(HDC hdc, const RECT& rect, const NetworkTelemetry& network) {
    DrawPanel(hdc, rect, L"Network");
    RECT upRect{rect.left + 16, rect.top + 38, rect.right - 16, rect.top + 62};
    RECT downRect{rect.left + 16, rect.top + 64, rect.right - 16, rect.top + 88};
    DrawTextBlock(hdc, upRect, L"Up   " + FormatSpeed(network.uploadMbps), fonts_.value, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, downRect, L"Down " + FormatSpeed(network.downloadMbps), fonts_.value, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const double maxGraph = std::max({10.0, network.uploadMbps * 1.5, network.downloadMbps * 1.5});
    RECT uploadGraph{rect.left + 16, rect.top + 98, rect.right - 16, rect.top + 128};
    RECT downloadGraph{rect.left + 16, rect.top + 136, rect.right - 16, rect.top + 166};
    DrawGraph(hdc, uploadGraph, network.uploadHistory, maxGraph);
    DrawGraph(hdc, downloadGraph, network.downloadHistory, maxGraph);

    RECT nameRect{rect.left + 16, rect.bottom - 46, rect.right - 16, rect.bottom - 24};
    RECT ipRect{rect.left + 16, rect.bottom - 24, rect.right - 16, rect.bottom - 6};
    DrawTextBlock(hdc, nameRect, network.adapterName, fonts_.smallFont, kMuted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, ipRect, network.ipAddress, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DashboardApp::DrawStoragePanel(HDC hdc, const RECT& rect, const std::vector<DriveInfo>& drives) {
    DrawPanel(hdc, rect, L"Storage");
    int y = rect.top + 42;
    for (const auto& drive : drives) {
        RECT labelRect{rect.left + 16, y, rect.left + 42, y + 20};
        RECT pctRect{rect.right - 140, y, rect.right - 94, y + 20};
        RECT freeRect{rect.right - 92, y, rect.right - 16, y + 20};
        RECT barRect{rect.left + 48, y + 4, rect.right - 150, y + 16};

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        HBRUSH track = CreateSolidBrush(kTrack);
        FillRect(hdc, &barRect, track);
        DeleteObject(track);

        RECT fill = barRect;
        fill.right = fill.left + static_cast<int>((fill.right - fill.left) * (drive.usedPercent / 100.0));
        HBRUSH accent = CreateSolidBrush(kAccent);
        FillRect(hdc, &fill, accent);
        DeleteObject(accent);

        wchar_t percent[16];
        swprintf_s(percent, L"%.0f%%", drive.usedPercent);
        DrawTextBlock(hdc, pctRect, percent, fonts_.label, kWhite, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        DrawTextBlock(hdc, freeRect, FormatDriveFree(drive.freeGb), fonts_.smallFont, kMuted,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        y += 34;
    }
}

void DashboardApp::DrawTimePanel(HDC hdc, const RECT& rect, const SYSTEMTIME& now) {
    DrawPanel(hdc, rect, L"Time");
    wchar_t timeBuffer[32];
    wchar_t dateBuffer[32];
    swprintf_s(timeBuffer, L"%02d:%02d", now.wHour, now.wMinute);
    swprintf_s(dateBuffer, L"%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);

    RECT timeRect{rect.left + 16, rect.top + 46, rect.right - 16, rect.top + 116};
    RECT dateRect{rect.left + 16, rect.top + 120, rect.right - 16, rect.top + 148};
    DrawTextBlock(hdc, timeRect, timeBuffer, fonts_.big, kWhite, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, dateRect, dateBuffer, fonts_.value, kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
}

void DashboardApp::DrawLayout(HDC hdc, const SystemSnapshot& snapshot) {
    const RECT cpuRect{10, 10, 395, 270};
    const RECT gpuRect{405, 10, 790, 270};
    const RECT networkRect{10, 280, 210, 470};
    const RECT storageRect{220, 280, 618, 470};
    const RECT timeRect{628, 280, 790, 470};

    DrawProcessorPanel(hdc, cpuRect, snapshot.cpu);
    DrawGpuPanel(hdc, gpuRect, snapshot.gpu);
    DrawNetworkPanel(hdc, networkRect, snapshot.network);
    DrawStoragePanel(hdc, storageRect, snapshot.drives);
    DrawTimePanel(hdc, timeRect, snapshot.now);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (HasSwitch(L"/dump")) {
        return RunDumpMode();
    }

    ShutdownPreviousInstance();

    DashboardApp app;
    if (!app.Initialize(instance)) {
        MessageBoxW(nullptr, L"Failed to initialize the telemetry dashboard.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
