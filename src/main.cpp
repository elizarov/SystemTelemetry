#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../resources/resource.h"
#include "config.h"
#include "telemetry.h"
#include "trace.h"
#include "utf8.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
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

COLORREF GetUsageFillColor() {
    return kAccent;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsInsensitive(const std::string& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::string::npos;
}

std::string FormatValue(const ScalarMetric& metric, int precision = 1) {
    if (!metric.value.has_value()) {
        return "N/A";
    }
    char buffer[64];
    sprintf_s(buffer, "%.*f %s", precision, *metric.value, metric.unit.c_str());
    return buffer;
}

std::string FormatMemory(double usedGb, double totalGb) {
    char buffer[64];
    sprintf_s(buffer, "%.1f / %.0f GB", usedGb, totalGb);
    return buffer;
}

std::string FormatDriveFree(double freeGb) {
    char buffer[64];
    if (freeGb >= 1024.0) {
        sprintf_s(buffer, "%.1f TB free", freeGb / 1024.0);
    } else {
        sprintf_s(buffer, "%.0f GB free", freeGb);
    }
    return buffer;
}

std::filesystem::path GetRuntimeConfigPath();
class DashboardApp;
bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
bool SaveConfigElevated(const AppConfig& config, HWND owner);

int GetImageEncoderClsid(const WCHAR* mimeType, CLSID* clsid) {
    UINT encoderCount = 0;
    UINT encoderBytes = 0;
    if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) != Gdiplus::Ok || encoderBytes == 0) {
        return -1;
    }

    std::vector<BYTE> encoderBuffer(encoderBytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoderBuffer.data());
    if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) != Gdiplus::Ok) {
        return -1;
    }

    for (UINT i = 0; i < encoderCount; ++i) {
        if (wcscmp(encoders[i].MimeType, mimeType) == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }

    return -1;
}

enum class PanelIcon : size_t {
    Cpu = 0,
    Gpu,
    Network,
    Storage,
    Time,
};

constexpr size_t kPanelIconCount = 5;

UINT GetPanelIconResourceId(PanelIcon icon) {
    switch (icon) {
    case PanelIcon::Cpu:
        return IDR_PANEL_ICON_CPU;
    case PanelIcon::Gpu:
        return IDR_PANEL_ICON_GPU;
    case PanelIcon::Network:
        return IDR_PANEL_ICON_NETWORK;
    case PanelIcon::Storage:
        return IDR_PANEL_ICON_STORAGE;
    case PanelIcon::Time:
        return IDR_PANEL_ICON_TIME;
    }
    return 0;
}

std::unique_ptr<Gdiplus::Bitmap> LoadPngResourceBitmap(UINT resourceId) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return nullptr;
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), L"PNG");
    if (resource == nullptr) {
        return nullptr;
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    HGLOBAL loadedResource = LoadResource(module, resource);
    if (loadedResource == nullptr || resourceSize == 0) {
        return nullptr;
    }

    const void* resourceData = LockResource(loadedResource);
    if (resourceData == nullptr) {
        return nullptr;
    }

    HGLOBAL copyHandle = GlobalAlloc(GMEM_MOVEABLE, resourceSize);
    if (copyHandle == nullptr) {
        return nullptr;
    }

    void* copyData = GlobalLock(copyHandle);
    if (copyData == nullptr) {
        GlobalFree(copyHandle);
        return nullptr;
    }

    memcpy(copyData, resourceData, resourceSize);
    GlobalUnlock(copyHandle);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(copyHandle, TRUE, &stream) != S_OK || stream == nullptr) {
        GlobalFree(copyHandle);
        return nullptr;
    }

    std::unique_ptr<Gdiplus::Bitmap> decoded(Gdiplus::Bitmap::FromStream(stream));
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    if (decoded != nullptr && decoded->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Rect rect(0, 0, decoded->GetWidth(), decoded->GetHeight());
        bitmap.reset(decoded->Clone(rect, PixelFormat32bppARGB));
        if (bitmap != nullptr && bitmap->GetLastStatus() != Gdiplus::Ok) {
            bitmap.reset();
        }
    }

    stream->Release();
    return bitmap;
}

std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(modulePath).parent_path();
}

std::vector<std::wstring> GetCommandLineArguments() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return {};
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return arguments;
}

bool HasSwitch(const std::string& target) {
    const std::wstring wideTarget = WideFromUtf8(target);
    for (const std::wstring& argument : GetCommandLineArguments()) {
        if (_wcsicmp(argument.c_str(), wideTarget.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::wstring> GetSwitchValue(const std::wstring& target) {
    const std::vector<std::wstring> arguments = GetCommandLineArguments();
    for (size_t i = 0; i + 1 < arguments.size(); ++i) {
        if (_wcsicmp(arguments[i].c_str(), target.c_str()) == 0) {
            return arguments[i + 1];
        }
    }
    return std::nullopt;
}

bool CanWriteRuntimeConfig(const std::filesystem::path& path) {
    const std::wstring widePath = path.wstring();
    if (std::filesystem::exists(path)) {
        HANDLE file = CreateFileW(
            widePath.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        CloseHandle(file);
        return true;
    }

    const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    const std::wstring probeName =
        L".config-write-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
    const std::filesystem::path probePath = parent / probeName;
    HANDLE probe = CreateFileW(
        probePath.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(probe);
    return true;
}

std::filesystem::path CreateElevatedSaveConfigTempPath() {
    wchar_t tempPathBuffer[MAX_PATH];
    const DWORD length = GetTempPathW(ARRAYSIZE(tempPathBuffer), tempPathBuffer);
    if (length == 0 || length >= ARRAYSIZE(tempPathBuffer)) {
        return {};
    }

    wchar_t tempFileBuffer[MAX_PATH];
    if (GetTempFileNameW(tempPathBuffer, L"stc", 0, tempFileBuffer) == 0) {
        return {};
    }
    return std::filesystem::path(tempFileBuffer);
}

int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath) {
    if (sourcePath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourcePath);
    if (!SaveConfig(GetRuntimeConfigPath(), config)) {
        return 1;
    }

    std::error_code ignored;
    std::filesystem::remove(sourcePath, ignored);
    return 0;
}

int RunDumpMode() {
    TelemetryCollector telemetry;
    const AppConfig config = LoadConfig(GetRuntimeConfigPath());
    const std::filesystem::path dumpPath = GetExecutableDirectory() / L"telemetry_dump.txt";
    const std::filesystem::path screenshotPath = GetExecutableDirectory() / L"telemetry_screenshot.png";
    std::ofstream dumpStream(dumpPath, std::ios::binary | std::ios::trunc);
    if (!dumpStream.is_open()) {
        const std::wstring message = WideFromUtf8("Failed to open dump file:\n" + Utf8FromWide(dumpPath.wstring()));
        MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return 1;
    }

    tracing::Trace trace(&dumpStream);
    trace.Write("dump:start");
    trace.Write("dump:telemetry_initialize_begin");

    if (!telemetry.Initialize(config, &dumpStream)) {
        trace.Write("dump:telemetry_initialize_failed");
        MessageBoxW(nullptr, L"Failed to initialize telemetry collector.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }

    trace.Write("dump:telemetry_initialized");
    Sleep(900);
    trace.Write("dump:update_snapshot_1_begin");
    telemetry.UpdateSnapshot();
    trace.Write("dump:update_snapshot_1_done");
    Sleep(1100);
    trace.Write("dump:update_snapshot_2_begin");
    telemetry.UpdateSnapshot();
    trace.Write("dump:update_snapshot_2_done");
    trace.Write("dump:write_dump_begin");
    dumpStream << "\n";
    telemetry.DumpText(dumpStream);
    trace.Write("dump:write_dump_done");

    trace.Write("dump:render_screenshot_begin");
    if (!SaveDumpScreenshot(screenshotPath, telemetry.Snapshot())) {
        trace.Write("dump:render_screenshot_failed reason=save_png");
        const std::wstring message =
            WideFromUtf8("Failed to save dump screenshot:\n" + Utf8FromWide(screenshotPath.wstring()));
        MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return 1;
    }

    trace.Write("dump:render_screenshot_done");
    trace.Write("dump:done");
    return 0;
}

std::string FormatSpeed(double mbps) {
    char buffer[64];
    if (mbps >= 100.0) {
        sprintf_s(buffer, "%.0f MB/s", mbps);
    } else {
        sprintf_s(buffer, "%.1f MB/s", mbps);
    }
    return buffer;
}

double GetNetworkGraphMax(double uploadMbps, double downloadMbps) {
    const double rawMax = std::max(10.0, std::max(uploadMbps, downloadMbps));
    return std::max(10.0, std::ceil(rawMax / 5.0) * 5.0);
}

struct MonitorPlacementInfo {
    std::string deviceName;
    std::string monitorName = "Unknown";
    std::string configMonitorName;
    RECT monitorRect{};
    POINT relativePosition{};
};

struct MonitorIdentity {
    std::string displayName;
    std::string configName;
};

std::string SimplifyDeviceName(const std::string& deviceName) {
    if (deviceName.rfind("\\\\.\\", 0) == 0) {
        return deviceName.substr(4);
    }
    return deviceName;
}

bool IsUsefulFriendlyName(const std::string& name) {
    const std::string lowered = ToLower(name);
    return !name.empty() &&
        lowered != "generic pnp monitor" &&
        lowered.find("\\\\?\\display") != 0;
}

MonitorIdentity GetMonitorIdentity(const std::string& deviceName);

std::optional<RECT> FindTargetMonitor(const std::string& requestedName) {
    if (requestedName.empty()) {
        return std::nullopt;
    }
    struct SearchContext {
        std::string requestedName;
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

            const std::string deviceName = Utf8FromWide(info.szDevice);
            const MonitorIdentity identity = GetMonitorIdentity(deviceName);
            if (ContainsInsensitive(identity.displayName, context->requestedName) ||
                ContainsInsensitive(identity.configName, context->requestedName) ||
                ContainsInsensitive(deviceName, context->requestedName)) {
                context->result = info.rcMonitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));

    return context.result;
}

MonitorIdentity GetMonitorIdentity(const std::string& deviceName) {
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

        const std::wstring wideDeviceName = WideFromUtf8(deviceName);
        if (_wcsicmp(sourceName.viewGdiDeviceName, wideDeviceName.c_str()) != 0) {
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

        const std::string friendlyName = Utf8FromWide(targetName.monitorFriendlyDeviceName);
        const std::string monitorPath = Utf8FromWide(targetName.monitorDevicePath);
        if (IsUsefulFriendlyName(friendlyName)) {
            identity.displayName = friendlyName + " (" + SimplifyDeviceName(deviceName) + ")";
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
        info.deviceName = Utf8FromWide(monitorInfo.szDevice);
        const MonitorIdentity identity = GetMonitorIdentity(info.deviceName);
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
    return GetExecutableDirectory() / L"config.ini";
}

class DashboardApp {
public:
    bool Initialize(HINSTANCE instance);
    int Run();
    bool InitializeFonts();
    void ReleaseFonts();
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);

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
    bool InitializeGdiplus();
    void ShutdownGdiplus();
    bool LoadPanelIcons();
    void ReleasePanelIcons();
    HICON LoadAppIcon(int width, int height);

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
        COLORREF color, UINT format);
    void DrawPanel(HDC hdc, const RECT& rect, const std::string& title, PanelIcon icon);
    void DrawPanelIcon(HDC hdc, PanelIcon icon, const RECT& iconRect);
    POINT PolarPoint(int cx, int cy, int radius, double angleDegrees);
    void DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label);
    void DrawMetricRow(HDC hdc, const RECT& rect, const std::string& label, const std::string& value, double ratio);
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
    ULONG_PTR gdiplusToken_ = 0;
    std::array<std::unique_ptr<Gdiplus::Bitmap>, kPanelIconCount> panelIcons_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
};

bool DashboardApp::Initialize(HINSTANCE instance) {
    instance_ = instance;
    config_ = LoadConfig(GetRuntimeConfigPath());
    telemetry_.Initialize(config_);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBlack);
    appIconLarge_ = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    appIconSmall_ = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.hIcon = appIconLarge_;
    wc.hIconSm = appIconSmall_;
    if (!RegisterClassExW(&wc)) {
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

bool DashboardApp::InitializeFonts() {
    if (!InitializeGdiplus() || !LoadPanelIcons()) {
        return false;
    }

    if (fonts_.title != nullptr) {
        return true;
    }

    fonts_.title = CreateUiFont(18, FW_BOLD, L"Segoe UI Semibold");
    fonts_.big = CreateUiFont(40, FW_BOLD, L"Segoe UI Semibold");
    fonts_.value = CreateUiFont(17, FW_BOLD, L"Segoe UI Semibold");
    fonts_.label = CreateUiFont(14, FW_NORMAL, L"Segoe UI");
    fonts_.smallFont = CreateUiFont(12, FW_NORMAL, L"Segoe UI");
    if (fonts_.title == nullptr || fonts_.big == nullptr || fonts_.value == nullptr ||
        fonts_.label == nullptr || fonts_.smallFont == nullptr) {
        ReleaseFonts();
        return false;
    }
    return true;
}

void DashboardApp::ReleaseFonts() {
    DeleteObject(fonts_.title);
    DeleteObject(fonts_.big);
    DeleteObject(fonts_.value);
    DeleteObject(fonts_.label);
    DeleteObject(fonts_.smallFont);
    fonts_.title = nullptr;
    fonts_.big = nullptr;
    fonts_.value = nullptr;
    fonts_.label = nullptr;
    fonts_.smallFont = nullptr;
    ReleasePanelIcons();
    ShutdownGdiplus();
}

bool DashboardApp::InitializeGdiplus() {
    if (gdiplusToken_ != 0) {
        return true;
    }

    Gdiplus::GdiplusStartupInput startupInput;
    return Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) == Gdiplus::Ok;
}

void DashboardApp::ShutdownGdiplus() {
    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

bool DashboardApp::LoadPanelIcons() {
    if (panelIcons_[0] != nullptr) {
        return true;
    }

    for (size_t index = 0; index < kPanelIconCount; ++index) {
        const auto icon = static_cast<PanelIcon>(index);
        panelIcons_[index] = LoadPngResourceBitmap(GetPanelIconResourceId(icon));
        if (panelIcons_[index] == nullptr) {
            ReleasePanelIcons();
            return false;
        }
    }

    return true;
}

void DashboardApp::ReleasePanelIcons() {
    for (auto& icon : panelIcons_) {
        icon.reset();
    }
}

HICON DashboardApp::LoadAppIcon(int width, int height) {
    return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        width, height, LR_DEFAULTCOLOR));
}

bool DashboardApp::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    if (!InitializeFonts()) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return false;
    }

    HDC memDc = CreateCompatibleDC(screenDc);
    if (memDc == nullptr) {
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = kWindowWidth;
    bitmapInfo.bmiHeader.biHeight = -kWindowHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap == nullptr) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
    RECT client{0, 0, kWindowWidth, kWindowHeight};
    HBRUSH background = CreateSolidBrush(kBlack);
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);
    DrawLayout(memDc, snapshot);

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR gdiplusToken = 0;
    bool saved = false;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) == Gdiplus::Ok) {
        CLSID pngClsid{};
        if (GetImageEncoderClsid(L"image/png", &pngClsid) >= 0) {
            Gdiplus::Bitmap image(bitmap, nullptr);
            saved = image.Save(imagePath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
        }
        Gdiplus::GdiplusShutdown(gdiplusToken);
    }

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return saved;
}

bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    DashboardApp renderer;
    if (!renderer.InitializeFonts()) {
        return false;
    }

    const bool saved = renderer.SaveSnapshotPng(imagePath, snapshot);
    renderer.ReleaseFonts();
    return saved;
}

void DashboardApp::BringOnTop() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
}

void DashboardApp::UpdateConfigFromCurrentPlacement() {
    const MonitorPlacementInfo placement = GetMonitorPlacementForWindow(hwnd_);
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    AppConfig config = telemetry_.EffectiveConfig();
    const std::string monitorName = !placement.configMonitorName.empty()
        ? placement.configMonitorName
        : placement.deviceName;
    config.monitorName = monitorName;
    config.positionX = placement.relativePosition.x;
    config.positionY = placement.relativePosition.y;
    const bool saved = CanWriteRuntimeConfig(configPath)
        ? SaveConfig(configPath, config)
        : SaveConfigElevated(config, hwnd_);
    if (!saved) {
        const std::wstring message = WideFromUtf8("Failed to update " + Utf8FromWide(configPath.wstring()) + ".");
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return;
    }

    config_.monitorName = monitorName;
    config_.positionX = placement.relativePosition.x;
    config_.positionY = placement.relativePosition.y;
}

bool SaveConfigElevated(const AppConfig& config, HWND owner) {
    const std::filesystem::path tempPath = CreateElevatedSaveConfigTempPath();
    if (tempPath.empty()) {
        return false;
    }

    if (!SaveConfig(tempPath, config)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    std::wstring parameters = L"/save-config \"";
    parameters += tempPath.wstring();
    parameters += L"\"";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }
    const std::wstring executablePath = modulePath;
    executeInfo.lpFile = executablePath.c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);

    std::error_code ignored;
    std::filesystem::remove(tempPath, ignored);
    return exitCode == 0;
}

bool DashboardApp::CreateTrayIcon() {
    trayIcon_ = {};
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    trayIcon_.hIcon = appIconSmall_ != nullptr ? appIconSmall_ : LoadIconW(nullptr, IDI_APPLICATION);
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

    char positionText[96];
    sprintf_s(positionText, "Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);

    DrawTextBlock(hdc, titleRect, "Move Mode", fonts_.label, kAccent, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, "Monitor: " + movePlacementInfo_.monitorName, fonts_.smallFont, kWhite,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, fonts_.smallFont, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, "Left-click to place. Copy monitor name and x/y into config.", fonts_.smallFont,
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
        if (!InitializeFonts()) {
            return -1;
        }
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
        if (lParam == WM_LBUTTONDBLCLK) {
            BringOnTop();
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
        ReleaseFonts();
        {
            HICON largeIcon = appIconLarge_;
            HICON smallIcon = appIconSmall_;
            appIconLarge_ = nullptr;
            appIconSmall_ = nullptr;
            if (largeIcon != nullptr) {
                DestroyIcon(largeIcon);
            }
            if (smallIcon != nullptr && smallIcon != largeIcon) {
                DestroyIcon(smallIcon);
            }
        }
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

void DashboardApp::DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
    COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    const std::wstring wideText = WideFromUtf8(text);
    DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
}

void DashboardApp::DrawPanelIcon(HDC hdc, PanelIcon icon, const RECT& iconRect) {
    const auto& bitmap = panelIcons_[static_cast<size_t>(icon)];
    if (bitmap == nullptr) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage(bitmap.get(),
        static_cast<INT>(iconRect.left),
        static_cast<INT>(iconRect.top),
        static_cast<INT>(iconRect.right - iconRect.left),
        static_cast<INT>(iconRect.bottom - iconRect.top));
}

void DashboardApp::DrawPanel(HDC hdc, const RECT& rect, const std::string& title, PanelIcon icon) {
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
    titleRect.left += 40;
    titleRect.top += 8;
    titleRect.right -= 14;
    titleRect.bottom = titleRect.top + 24;

    const int titleCenterY = (titleRect.top + titleRect.bottom) / 2;
    RECT iconRect{rect.left + 14, titleCenterY - 10, rect.left + 34, titleCenterY + 10};
    DrawPanelIcon(hdc, icon, iconRect);

    DrawTextBlock(hdc, titleRect, title, fonts_.title, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

POINT DashboardApp::PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{
        cx + static_cast<LONG>(std::round(std::cos(radians) * radius)),
        cy - static_cast<LONG>(std::round(std::sin(radians) * radius))
    };
}

void DashboardApp::DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label) {
    HPEN trackPen = CreatePen(PS_SOLID, 10, kTrack);
    HPEN usagePen = CreatePen(PS_SOLID, 10, GetUsageFillColor());
    HGDIOBJ oldPen = SelectObject(hdc, trackPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    const RECT bounds{cx - radius, cy - radius, cx + radius, cy + radius};
    Ellipse(hdc, bounds.left, bounds.top, bounds.right, bounds.bottom);

    const double clampedPercent = std::clamp(percent, 0.0, 100.0);
    const double sweep = 360.0 * clampedPercent / 100.0;
    if (sweep > 0.0) {
        SelectObject(hdc, usagePen);
        SetArcDirection(hdc, AD_CLOCKWISE);
        const POINT startValue = PolarPoint(cx, cy, radius, 90.0);
        MoveToEx(hdc, startValue.x, startValue.y, nullptr);
        AngleArc(hdc, cx, cy, radius, 90.0f, static_cast<FLOAT>(-sweep));
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(trackPen);
    DeleteObject(usagePen);

    RECT numberRect{cx - 42, cy - 28, cx + 42, cy + 18};
    char number[16];
    sprintf_s(number, "%.0f%%", percent);
    DrawTextBlock(hdc, numberRect, number, fonts_.big, kWhite, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect{cx - 42, cy + 18, cx + 42, cy + 42};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, kMuted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DashboardApp::DrawMetricRow(
    HDC hdc, const RECT& rect, const std::string& label, const std::string& value, double ratio) {
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
    HBRUSH accent = CreateSolidBrush(GetUsageFillColor());
    FillRect(hdc, &fill, accent);
    DeleteObject(accent);
}

void DashboardApp::DrawProcessorPanel(HDC hdc, const RECT& rect, const ProcessorTelemetry& cpu) {
    DrawPanel(hdc, rect, "CPU", PanelIcon::Cpu);
    RECT nameRect{rect.left + 16, rect.top + 34, rect.right - 16, rect.top + 58};
    DrawTextBlock(hdc, nameRect, cpu.name, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGauge(hdc, rect.left + 92, rect.top + 132, 52, cpu.loadPercent, "Load");

    int y = rect.top + 92;
    const int rowHeight = 34;
    RECT rows{rect.left + 164, y, rect.right - 18, y + rowHeight};
    DrawMetricRow(hdc, rows, "Temp", FormatValue(cpu.temperature, 0), cpu.temperature.value.value_or(0.0) / 100.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, "Clock", FormatValue(cpu.clock, 2), cpu.clock.value.value_or(0.0) / 5.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, "Fan", FormatValue(cpu.fan, 0), cpu.fan.value.value_or(0.0) / 3000.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, "RAM", FormatMemory(cpu.memory.usedGb, cpu.memory.totalGb),
        cpu.memory.totalGb > 0.0 ? cpu.memory.usedGb / cpu.memory.totalGb : 0.0);
}

void DashboardApp::DrawGpuPanel(HDC hdc, const RECT& rect, const GpuTelemetry& gpu) {
    DrawPanel(hdc, rect, "GPU", PanelIcon::Gpu);
    RECT nameRect{rect.left + 16, rect.top + 34, rect.right - 16, rect.top + 58};
    DrawTextBlock(hdc, nameRect, gpu.name, fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawGauge(hdc, rect.left + 92, rect.top + 132, 52, gpu.loadPercent, "Load");

    int y = rect.top + 92;
    const int rowHeight = 34;
    RECT rows{rect.left + 164, y, rect.right - 18, y + rowHeight};
    DrawMetricRow(hdc, rows, "Temp", FormatValue(gpu.temperature, 0), gpu.temperature.value.value_or(0.0) / 100.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, "Clock", FormatValue(gpu.clock, 0), gpu.clock.value.value_or(0.0) / 2600.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, "Fan", FormatValue(gpu.fan, 0), gpu.fan.value.value_or(0.0) / 3000.0);
    OffsetRect(&rows, 0, rowHeight);
    DrawMetricRow(hdc, rows, "VRAM", FormatMemory(gpu.vram.usedGb, std::max(1.0, gpu.vram.totalGb)),
        gpu.vram.totalGb > 0.0 ? gpu.vram.usedGb / gpu.vram.totalGb : 0.0);
}

void DashboardApp::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue) {
    HBRUSH bg = CreateSolidBrush(RGB(10, 12, 15));
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    const int graphLeft = rect.left + 18;
    const int width = std::max<int>(1, rect.right - graphLeft - 1);
    const int height = std::max<int>(1, rect.bottom - rect.top - 1);
    const int graphRight = graphLeft + width;
    const int graphBottom = rect.bottom - 1;

    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(42, 48, 54));
    HGDIOBJ oldPen = SelectObject(hdc, gridPen);
    for (double tick = 5.0; tick < maxValue; tick += 5.0) {
        const double ratio = tick / maxValue;
        const int y = graphBottom - static_cast<int>(std::round(ratio * height));
        MoveToEx(hdc, graphLeft, y, nullptr);
        LineTo(hdc, graphRight, y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(80, 88, 96));
    oldPen = SelectObject(hdc, axisPen);
    MoveToEx(hdc, rect.left + 18, rect.top, nullptr);
    LineTo(hdc, rect.left + 18, rect.bottom - 1);
    MoveToEx(hdc, rect.left + 18, rect.bottom - 1, nullptr);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(axisPen);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top + 1, rect.left + 18, rect.top + 13};
    DrawTextBlock(hdc, maxRect, maxLabel, fonts_.smallFont, kWhite, DT_CENTER | DT_SINGLELINE | DT_TOP);

    HPEN pen = CreatePen(PS_SOLID, 2, kAccent);
    oldPen = SelectObject(hdc, pen);
    for (size_t i = 1; i < history.size(); ++i) {
        const double v1 = std::clamp(history[i - 1] / maxValue, 0.0, 1.0);
        const double v2 = std::clamp(history[i] / maxValue, 0.0, 1.0);
        const int x1 = graphLeft + static_cast<int>((i - 1) * width / std::max<size_t>(1, history.size() - 1));
        const int x2 = graphLeft + static_cast<int>(i * width / std::max<size_t>(1, history.size() - 1));
        const int y1 = rect.bottom - 1 - static_cast<int>(v1 * height);
        const int y2 = rect.bottom - 1 - static_cast<int>(v2 * height);
        MoveToEx(hdc, x1, y1, nullptr);
        LineTo(hdc, x2, y2);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DashboardApp::DrawNetworkPanel(HDC hdc, const RECT& rect, const NetworkTelemetry& network) {
    DrawPanel(hdc, rect, "Network", PanelIcon::Network);
    RECT upRect{rect.left + 16, rect.top + 38, rect.right - 16, rect.top + 54};
    RECT uploadGraph{rect.left + 16, rect.top + 56, rect.right - 16, rect.top + 97};
    RECT downRect{rect.left + 16, rect.top + 102, rect.right - 16, rect.top + 118};
    RECT downloadGraph{rect.left + 16, rect.top + 120, rect.right - 16, rect.bottom - 28};
    RECT footerRect{rect.left + 16, rect.bottom - 22, rect.right - 16, rect.bottom - 6};
    const double maxGraph = GetNetworkGraphMax(network.uploadMbps, network.downloadMbps);

    RECT upLabelRect{upRect.left, upRect.top, upRect.left + 42, upRect.bottom};
    RECT upValueRect{upRect.left + 44, upRect.top, upRect.right, upRect.bottom};
    RECT downLabelRect{downRect.left, downRect.top, downRect.left + 54, downRect.bottom};
    RECT downValueRect{downRect.left + 56, downRect.top, downRect.right, downRect.bottom};

    DrawTextBlock(hdc, upLabelRect, "Up", fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, upValueRect, FormatSpeed(network.uploadMbps), fonts_.label, kWhite,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawGraph(hdc, uploadGraph, network.uploadHistory, maxGraph);

    DrawTextBlock(hdc, downLabelRect, "Down", fonts_.label, kWhite, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, downValueRect, FormatSpeed(network.downloadMbps), fonts_.label, kWhite,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawGraph(hdc, downloadGraph, network.downloadHistory, maxGraph);

    const std::string footer = network.adapterName.empty()
        ? network.ipAddress
        : network.adapterName + " | " + network.ipAddress;
    DrawTextBlock(hdc, footerRect, footer, fonts_.smallFont, kWhite, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DashboardApp::DrawStoragePanel(HDC hdc, const RECT& rect, const std::vector<DriveInfo>& drives) {
    DrawPanel(hdc, rect, "Storage", PanelIcon::Storage);
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
        HBRUSH accent = CreateSolidBrush(GetUsageFillColor());
        FillRect(hdc, &fill, accent);
        DeleteObject(accent);

        char percent[16];
        sprintf_s(percent, "%.0f%%", drive.usedPercent);
        DrawTextBlock(hdc, pctRect, percent, fonts_.label, kWhite, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        DrawTextBlock(hdc, freeRect, FormatDriveFree(drive.freeGb), fonts_.smallFont, kMuted,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        y += 34;
    }
}

void DashboardApp::DrawTimePanel(HDC hdc, const RECT& rect, const SYSTEMTIME& now) {
    DrawPanel(hdc, rect, "Time", PanelIcon::Time);
    char timeBuffer[32];
    char dateBuffer[32];
    sprintf_s(timeBuffer, "%02d:%02d", now.wHour, now.wMinute);
    sprintf_s(dateBuffer, "%04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);

    RECT timeRect{rect.left + 16, rect.top + 46, rect.right - 16, rect.top + 116};
    RECT dateRect{rect.left + 16, rect.top + 120, rect.right - 16, rect.top + 148};
    DrawTextBlock(hdc, timeRect, timeBuffer, fonts_.big, kWhite, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, dateRect, dateBuffer, fonts_.value, kMuted, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
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
    if (const auto elevatedSaveSource = GetSwitchValue(L"/save-config"); elevatedSaveSource.has_value()) {
        return RunElevatedSaveConfigMode(*elevatedSaveSource);
    }

    if (HasSwitch("/dump")) {
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
