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
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../resources/resource.h"
#include "config.h"
#include "snapshot_dump.h"
#include "telemetry.h"
#include "telemetry_runtime.h"
#include "trace.h"
#include "utf8.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT_PTR kMoveTimerId = 2;
constexpr UINT kRefreshTimerMs = 250;
constexpr UINT kMoveTimerMs = 16;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kCommandMove = 1001;
constexpr UINT kCommandBringOnTop = 1002;
constexpr UINT kCommandReloadConfig = 1003;
constexpr UINT kCommandSaveConfig = 1004;
constexpr UINT kCommandExit = 1005;
constexpr wchar_t kWindowClassName[] = L"SystemTelemetryDashboard";

COLORREF ToColorRef(unsigned int color) {
    return RGB((color >> 16) & 0xFFu, (color >> 8) & 0xFFu, color & 0xFFu);
}

COLORREF GetUsageFillColor(const AppConfig& config) {
    return ToColorRef(config.layout.accentColor);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
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
bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const AppConfig& config);
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);

DiagnosticsOptions GetDiagnosticsOptions();

class DiagnosticsSession {
public:
    explicit DiagnosticsSession(const DiagnosticsOptions& options);

    bool Initialize();
    std::ostream* TraceStream();
    void WriteTraceMarker(const std::string& text);
    bool WriteOutputs(const TelemetryDump& dump, const AppConfig& config);

private:
    static void ShowFileOpenError(const char* label, const std::filesystem::path& path);

    DiagnosticsOptions options_;
    std::filesystem::path tracePath_;
    std::filesystem::path dumpPath_;
    std::filesystem::path screenshotPath_;
    std::ofstream traceStream_;
};

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

UINT GetPanelIconResourceId(const std::string& iconName) {
    const std::string lowered = ToLower(iconName);
    if (lowered == "cpu") {
        return IDR_PANEL_ICON_CPU;
    }
    if (lowered == "gpu") {
        return IDR_PANEL_ICON_GPU;
    }
    if (lowered == "network") {
        return IDR_PANEL_ICON_NETWORK;
    }
    if (lowered == "storage") {
        return IDR_PANEL_ICON_STORAGE;
    }
    if (lowered == "time") {
        return IDR_PANEL_ICON_TIME;
    }
    return 0;
}

bool IsContainerNode(const LayoutNodeConfig& node) {
    const std::string lowered = ToLower(node.name);
    return lowered == "columns" || lowered == "stack" || lowered == "stack_top" || lowered == "center";
}

std::optional<std::string> GetNodeParameter(const LayoutNodeConfig& node, const std::string& key) {
    for (const auto& parameter : node.parameters) {
        if (ToLower(parameter.first) == ToLower(key)) {
            return parameter.second;
        }
    }
    return std::nullopt;
}

struct WidgetBinding {
    std::vector<std::string> items;
    std::vector<std::string> drives;
};

enum class WidgetKind {
    CpuName,
    GpuName,
    GaugeCpuLoad,
    GaugeGpuLoad,
    MetricListCpu,
    MetricListGpu,
    ThroughputUpload,
    ThroughputDownload,
    ThroughputRead,
    ThroughputWrite,
    NetworkFooter,
    Spacer,
    DriveUsageList,
    ClockTime,
    ClockDate,
    Unknown,
};

WidgetKind ParseWidgetKind(const std::string& name) {
    const std::string lowered = ToLower(name);
    if (lowered == "cpu_name") return WidgetKind::CpuName;
    if (lowered == "gpu_name") return WidgetKind::GpuName;
    if (lowered == "gauge_cpu_load") return WidgetKind::GaugeCpuLoad;
    if (lowered == "gauge_gpu_load") return WidgetKind::GaugeGpuLoad;
    if (lowered == "metric_list_cpu") return WidgetKind::MetricListCpu;
    if (lowered == "metric_list_gpu") return WidgetKind::MetricListGpu;
    if (lowered == "throughput_upload") return WidgetKind::ThroughputUpload;
    if (lowered == "throughput_download") return WidgetKind::ThroughputDownload;
    if (lowered == "throughput_read") return WidgetKind::ThroughputRead;
    if (lowered == "throughput_write") return WidgetKind::ThroughputWrite;
    if (lowered == "network_footer") return WidgetKind::NetworkFooter;
    if (lowered == "spacer") return WidgetKind::Spacer;
    if (lowered == "drive_usage_list") return WidgetKind::DriveUsageList;
    if (lowered == "clock_time") return WidgetKind::ClockTime;
    if (lowered == "clock_date") return WidgetKind::ClockDate;
    return WidgetKind::Unknown;
}

struct ResolvedWidgetLayout {
    WidgetKind kind = WidgetKind::Unknown;
    RECT rect{};
    WidgetBinding binding;
};

struct ResolvedCardLayout {
    std::string id;
    std::string title;
    std::string iconName;
    RECT rect{};
    RECT titleRect{};
    RECT iconRect{};
    RECT contentRect{};
    std::vector<ResolvedWidgetLayout> widgets;
};

struct ResolvedDashboardLayout {
    int windowWidth = 800;
    int windowHeight = 480;
    std::vector<ResolvedCardLayout> cards;
};

struct FontHeights {
    int title = 0;
    int big = 0;
    int value = 0;
    int label = 0;
    int smallText = 0;
};

std::vector<std::string> Split(const std::string& input, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        const std::string trimmed = Trim(item);
        if (!trimmed.empty()) {
            parts.push_back(trimmed);
        }
    }
    return parts;
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

DiagnosticsOptions GetDiagnosticsOptions() {
    DiagnosticsOptions options;
    options.trace = HasSwitch("/trace");
    options.dump = HasSwitch("/dump");
    options.screenshot = HasSwitch("/screenshot");
    options.exit = HasSwitch("/exit");
    options.fake = HasSwitch("/fake");
    return options;
}

DiagnosticsSession::DiagnosticsSession(const DiagnosticsOptions& options) : options_(options) {}

bool DiagnosticsSession::Initialize() {
    const std::filesystem::path executableDirectory = GetExecutableDirectory();
    if (options_.trace) {
        tracePath_ = executableDirectory / L"telemetry_trace.txt";
        traceStream_.open(tracePath_, std::ios::binary | std::ios::app);
        if (!traceStream_.is_open()) {
            ShowFileOpenError("trace file", tracePath_);
            return false;
        }
    }
    if (options_.dump) {
        dumpPath_ = executableDirectory / L"telemetry_dump.txt";
    }
    if (options_.screenshot) {
        screenshotPath_ = executableDirectory / L"telemetry_screenshot.png";
    }
    return true;
}

std::ostream* DiagnosticsSession::TraceStream() {
    return traceStream_.is_open() ? &traceStream_ : nullptr;
}

void DiagnosticsSession::WriteTraceMarker(const std::string& text) {
    if (!traceStream_.is_open()) {
        return;
    }
    tracing::Trace trace(&traceStream_);
    trace.Write(text);
}

bool DiagnosticsSession::WriteOutputs(const TelemetryDump& dump, const AppConfig& config) {
    if (options_.dump) {
        std::ofstream dumpStream(dumpPath_, std::ios::binary | std::ios::trunc);
        if (!dumpStream.is_open()) {
            ShowFileOpenError("dump file", dumpPath_);
            return false;
        }
        if (!WriteTelemetryDump(dumpStream, dump)) {
            const std::wstring message =
                WideFromUtf8("Failed to write dump file:\n" + Utf8FromWide(dumpPath_.wstring()));
            MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
            return false;
        }
    }

    if (options_.screenshot && !SaveDumpScreenshot(screenshotPath_, dump.snapshot, config)) {
        const std::wstring message =
            WideFromUtf8("Failed to save screenshot:\n" + Utf8FromWide(screenshotPath_.wstring()));
        MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return false;
    }

    return true;
}

void DiagnosticsSession::ShowFileOpenError(const char* label, const std::filesystem::path& path) {
    const std::wstring message = WideFromUtf8(
        std::string("Failed to open ") + label + ":\n" + Utf8FromWide(path.wstring()));
    MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
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

int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath) {
    if (sourcePath.empty() || targetPath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourcePath);
    if (!SaveConfig(targetPath, config)) {
        return 1;
    }

    std::error_code ignored;
    std::filesystem::remove(sourcePath, ignored);
    return 0;
}

int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions) {
    std::unique_ptr<TelemetryRuntime> telemetry = CreateTelemetryRuntime(diagnosticsOptions, GetExecutableDirectory());
    const AppConfig config = LoadConfig(GetRuntimeConfigPath());
    DiagnosticsSession diagnostics(diagnosticsOptions);
    if (!diagnostics.Initialize()) {
        return 1;
    }

    diagnostics.WriteTraceMarker("diagnostics:headless_start");
    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_begin");

    if (!telemetry->Initialize(config, diagnostics.TraceStream())) {
        diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_failed");
        MessageBoxW(nullptr, L"Failed to initialize telemetry collector.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }

    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialized");
    Sleep(1000);
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_begin");
    telemetry->UpdateSnapshot();
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_done");
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_begin");
    if (!diagnostics.WriteOutputs(telemetry->Dump(), telemetry->EffectiveConfig())) {
        diagnostics.WriteTraceMarker("diagnostics:write_outputs_failed");
        return 1;
    }
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_done");
    diagnostics.WriteTraceMarker("diagnostics:headless_done");
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

double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory) {
    double rawMax = 10.0;
    for (double value : firstHistory) {
        rawMax = std::max(rawMax, value);
    }
    for (double value : secondHistory) {
        rawMax = std::max(rawMax, value);
    }
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

HFONT CreateUiFont(const UiFontConfig& font) {
    const std::wstring face = WideFromUtf8(font.face);
    return CreateFontW(-font.size, 0, 0, 0, font.weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, face.c_str());
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
    explicit DashboardApp(const DiagnosticsOptions& diagnosticsOptions = {});
    bool Initialize(HINSTANCE instance);
    int Run();
    bool InitializeFonts();
    void SetRenderConfig(const AppConfig& config);
    void ReleaseFonts();
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool WriteDiagnosticsOutputs();

private:
    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void ShowContextMenu(POINT screenPoint);
    void BringOnTop();
    bool ReloadConfigFromDisk();
    void UpdateConfigFromCurrentPlacement();
    void ApplyConfigPlacement();
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
    COLORREF BackgroundColor() const;
    COLORREF ForegroundColor() const;
    COLORREF AccentColor() const;
    COLORREF PanelBorderColor() const;
    COLORREF MutedTextColor() const;
    COLORREF TrackColor() const;
    COLORREF PanelFillColor() const;
    COLORREF GraphBackgroundColor() const;
    COLORREF GraphGridColor() const;
    COLORREF GraphAxisColor() const;
    COLORREF UsageFillColor() const;
    int WindowWidth() const;
    int WindowHeight() const;

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
        COLORREF color, UINT format);
    void DrawPanel(HDC hdc, const ResolvedCardLayout& card);
    void DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect);
    POINT PolarPoint(int cx, int cy, int radius, double angleDegrees);
    void DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label);
    void DrawMetricRow(HDC hdc, const RECT& rect, const std::string& label, const std::string& value, double ratio);
    void DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue);
    void DrawThroughputWidget(HDC hdc, const RECT& rect, const char* label,
        double valueMbps, const std::vector<double>& history, double maxGraph);
    void DrawResolvedWidget(HDC hdc, const ResolvedWidgetLayout& widget, const SystemSnapshot& snapshot);
    void DrawMetricListWidget(HDC hdc, const RECT& rect, const std::vector<std::string>& items,
        bool isGpu, const SystemSnapshot& snapshot);
    void DrawDriveUsageWidget(HDC hdc, const RECT& rect, const std::vector<std::string>& drives,
        const std::vector<DriveInfo>& availableDrives);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);
    bool ResolveLayout();
    void ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets);
    int PreferredNodeHeight(const LayoutNodeConfig& node, int width) const;
    int EffectiveHeaderHeight() const;
    int EffectiveMetricRowHeight() const;
    bool MeasureFonts();

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
    std::unique_ptr<TelemetryRuntime> telemetry_;
    bool isMoving_ = false;
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
    ULONG_PTR gdiplusToken_ = 0;
    std::vector<std::pair<std::string, std::unique_ptr<Gdiplus::Bitmap>>> panelIcons_;
    FontHeights fontHeights_{};
    ResolvedDashboardLayout resolvedLayout_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
    DiagnosticsOptions diagnosticsOptions_;
    std::unique_ptr<DiagnosticsSession> diagnostics_;
    std::chrono::steady_clock::time_point lastDiagnosticsOutput_{};
};

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions) : diagnosticsOptions_(diagnosticsOptions) {}

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    config_ = config;
}

int DashboardApp::WindowWidth() const {
    return std::max(1, config_.layout.windowWidth);
}

int DashboardApp::WindowHeight() const {
    return std::max(1, config_.layout.windowHeight);
}

bool DashboardApp::Initialize(HINSTANCE instance) {
    instance_ = instance;
    config_ = LoadConfig(GetRuntimeConfigPath());
    telemetry_ = CreateTelemetryRuntime(diagnosticsOptions_, GetExecutableDirectory());
    if (diagnosticsOptions_.HasAnyOutput()) {
        diagnostics_ = std::make_unique<DiagnosticsSession>(diagnosticsOptions_);
        if (!diagnostics_->Initialize()) {
            return false;
        }
        diagnostics_->WriteTraceMarker("diagnostics:ui_start");
        diagnostics_->WriteTraceMarker("diagnostics:telemetry_initialize_begin");
    }
    if (telemetry_ == nullptr || !telemetry_->Initialize(config_, diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr)) {
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:telemetry_initialize_failed");
        }
        return false;
    }
    if (diagnostics_ != nullptr) {
        diagnostics_->WriteTraceMarker("diagnostics:telemetry_initialized");
        lastDiagnosticsOutput_ = std::chrono::steady_clock::now();
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(BackgroundColor());
    appIconLarge_ = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    appIconSmall_ = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.hIcon = appIconLarge_;
    wc.hIconSm = appIconSmall_;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    RECT placement{100, 100, 100 + WindowWidth(), 100 + WindowHeight()};
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
        WindowWidth(),
        WindowHeight(),
        nullptr,
        nullptr,
        instance,
        this);
    return hwnd_ != nullptr;
}

void DashboardApp::ApplyConfigPlacement() {
    int left = 100 + config_.positionX;
    int top = 100 + config_.positionY;
    if (const auto monitor = FindTargetMonitor(config_.monitorName); monitor.has_value()) {
        left = monitor->left + config_.positionX;
        top = monitor->top + config_.positionY;
    }
    SetWindowPos(hwnd_, HWND_TOP, left, top, WindowWidth(), WindowHeight(), SWP_NOACTIVATE);
}

bool DashboardApp::InitializeFonts() {
    if (!InitializeGdiplus() || !LoadPanelIcons()) {
        return false;
    }

    if (fonts_.title != nullptr) {
        return true;
    }

    fonts_.title = CreateUiFont(config_.layout.titleFont);
    fonts_.big = CreateUiFont(config_.layout.bigFont);
    fonts_.value = CreateUiFont(config_.layout.valueFont);
    fonts_.label = CreateUiFont(config_.layout.labelFont);
    fonts_.smallFont = CreateUiFont(config_.layout.smallFont);
    if (fonts_.title == nullptr || fonts_.big == nullptr || fonts_.value == nullptr ||
        fonts_.label == nullptr || fonts_.smallFont == nullptr) {
        ReleaseFonts();
        return false;
    }
    if (!MeasureFonts() || !ResolveLayout()) {
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
    if (!panelIcons_.empty()) {
        return true;
    }

    std::set<std::string> uniqueIcons;
    for (const auto& card : config_.layout.cards) {
        if (!card.icon.empty()) {
            uniqueIcons.insert(ToLower(card.icon));
        }
    }

    for (const auto& iconName : uniqueIcons) {
        const UINT resourceId = GetPanelIconResourceId(iconName);
        if (resourceId == 0) {
            ReleasePanelIcons();
            return false;
        }
        auto bitmap = LoadPngResourceBitmap(resourceId);
        if (bitmap == nullptr) {
            ReleasePanelIcons();
            return false;
        }
        panelIcons_.push_back({iconName, std::move(bitmap)});
    }

    return true;
}

void DashboardApp::ReleasePanelIcons() {
    panelIcons_.clear();
}

COLORREF DashboardApp::BackgroundColor() const {
    return ToColorRef(config_.layout.backgroundColor);
}

COLORREF DashboardApp::ForegroundColor() const {
    return ToColorRef(config_.layout.foregroundColor);
}

COLORREF DashboardApp::AccentColor() const {
    return ToColorRef(config_.layout.accentColor);
}

COLORREF DashboardApp::PanelBorderColor() const {
    return ToColorRef(config_.layout.panelBorderColor);
}

COLORREF DashboardApp::MutedTextColor() const {
    return ToColorRef(config_.layout.mutedTextColor);
}

COLORREF DashboardApp::TrackColor() const {
    return ToColorRef(config_.layout.trackColor);
}

COLORREF DashboardApp::PanelFillColor() const {
    return ToColorRef(config_.layout.panelFillColor);
}

COLORREF DashboardApp::GraphBackgroundColor() const {
    return ToColorRef(config_.layout.graphBackgroundColor);
}

COLORREF DashboardApp::GraphGridColor() const {
    return ToColorRef(config_.layout.graphGridColor);
}

COLORREF DashboardApp::GraphAxisColor() const {
    return ToColorRef(config_.layout.graphAxisColor);
}

COLORREF DashboardApp::UsageFillColor() const {
    return GetUsageFillColor(config_);
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
    bitmapInfo.bmiHeader.biWidth = WindowWidth();
    bitmapInfo.bmiHeader.biHeight = -WindowHeight();
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
    RECT client{0, 0, WindowWidth(), WindowHeight()};
    HBRUSH background = CreateSolidBrush(BackgroundColor());
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

bool DashboardApp::WriteDiagnosticsOutputs() {
    if (diagnostics_ == nullptr) {
        return true;
    }
    diagnostics_->WriteTraceMarker("diagnostics:write_outputs_begin");
    const bool ok = diagnostics_->WriteOutputs(telemetry_->Dump(), telemetry_->EffectiveConfig());
    diagnostics_->WriteTraceMarker(ok ? "diagnostics:write_outputs_done" : "diagnostics:write_outputs_failed");
    return ok;
}

bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const AppConfig& config) {
    DashboardApp renderer;
    renderer.SetRenderConfig(config);
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

bool DashboardApp::ReloadConfigFromDisk() {
    const AppConfig reloadedConfig = LoadConfig(GetRuntimeConfigPath());
    if (diagnostics_ != nullptr) {
        diagnostics_->WriteTraceMarker("diagnostics:reload_config_begin");
    }

    const auto initializeRuntime = [&](const AppConfig& runtimeConfig) -> std::unique_ptr<TelemetryRuntime> {
        std::unique_ptr<TelemetryRuntime> runtime =
            CreateTelemetryRuntime(diagnosticsOptions_, GetExecutableDirectory());
        if (runtime == nullptr) {
            return nullptr;
        }
        if (!runtime->Initialize(runtimeConfig, diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr)) {
            return nullptr;
        }
        return runtime;
    };

    telemetry_.reset();
    std::unique_ptr<TelemetryRuntime> reloadedTelemetry = initializeRuntime(reloadedConfig);
    if (reloadedTelemetry == nullptr) {
        telemetry_ = initializeRuntime(config_);
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }

    config_ = reloadedConfig;
    telemetry_ = std::move(reloadedTelemetry);
    telemetry_->UpdateSnapshot();
    ReleaseFonts();
    if (!InitializeFonts()) {
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    ApplyConfigPlacement();
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (diagnostics_ != nullptr) {
        diagnostics_->WriteTraceMarker("diagnostics:reload_config_done");
    }
    return true;
}

void DashboardApp::UpdateConfigFromCurrentPlacement() {
    const MonitorPlacementInfo placement = GetMonitorPlacementForWindow(hwnd_);
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    AppConfig config = telemetry_->EffectiveConfig();
    const std::string monitorName = !placement.configMonitorName.empty()
        ? placement.configMonitorName
        : placement.deviceName;
    config.monitorName = monitorName;
    config.positionX = placement.relativePosition.x;
    config.positionY = placement.relativePosition.y;
    bool saved = false;
    if (CanWriteRuntimeConfig(configPath)) {
        saved = SaveConfig(configPath, config);
    }
    if (!saved) {
        saved = SaveConfigElevated(configPath, config, hwnd_);
    }
    if (!saved) {
        const std::wstring message = WideFromUtf8("Failed to save " + Utf8FromWide(configPath.wstring()) + ".");
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return;
    }

    config_.monitorName = monitorName;
    config_.positionX = placement.relativePosition.x;
    config_.positionY = placement.relativePosition.y;
}

bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner) {
    const std::filesystem::path tempPath = CreateElevatedSaveConfigTempPath();
    if (tempPath.empty() || targetPath.empty()) {
        return false;
    }

    if (!SaveConfig(tempPath, config)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    std::wstring parameters = L"/save-config \"";
    parameters += tempPath.wstring();
    parameters += L"\" /save-config-target \"";
    parameters += targetPath.wstring();
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

    const int x = cursor.x - (WindowWidth() / 2);
    const int y = cursor.y - 24;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
}

void DashboardApp::ShowContextMenu(POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandReloadConfig, L"Reload Config");
    AppendMenuW(menu, MF_STRING, kCommandSaveConfig, L"Save Config");
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
    case kCommandReloadConfig:
        if (!ReloadConfigFromDisk()) {
            MessageBoxW(hwnd_, L"Failed to reload config.ini.", L"System Telemetry", MB_ICONERROR);
        }
        break;
    case kCommandSaveConfig:
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
    HBRUSH fill = CreateSolidBrush(BackgroundColor());
    HPEN border = CreatePen(PS_SOLID, 1, AccentColor());
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

    DrawTextBlock(hdc, titleRect, "Move Mode", fonts_.label, AccentColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, "Monitor: " + movePlacementInfo_.monitorName, fonts_.smallFont, ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, fonts_.smallFont, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, "Left-click to place. Copy monitor name and x/y into config.", fonts_.smallFont,
        MutedTextColor(), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
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
        telemetry_->UpdateSnapshot();
        if (diagnostics_ != nullptr &&
            std::chrono::steady_clock::now() - lastDiagnosticsOutput_ >= std::chrono::seconds(1)) {
            if (!WriteDiagnosticsOutputs()) {
                DestroyWindow(hwnd_);
                return 0;
            }
            lastDiagnosticsOutput_ = std::chrono::steady_clock::now();
        }
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
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:ui_done");
        }
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

    HBRUSH background = CreateSolidBrush(BackgroundColor());
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);

    DrawLayout(memDc, telemetry_->Snapshot());
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

void DashboardApp::DrawPanelIcon(HDC hdc, const std::string& iconName, const RECT& iconRect) {
    const auto it = std::find_if(panelIcons_.begin(), panelIcons_.end(), [&](const auto& entry) {
        return ToLower(entry.first) == ToLower(iconName);
    });
    if (it == panelIcons_.end() || it->second == nullptr) {
        return;
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.DrawImage(it->second.get(),
        static_cast<INT>(iconRect.left),
        static_cast<INT>(iconRect.top),
        static_cast<INT>(iconRect.right - iconRect.left),
        static_cast<INT>(iconRect.bottom - iconRect.top));
}

void DashboardApp::DrawPanel(HDC hdc, const ResolvedCardLayout& card) {
    HPEN border = CreatePen(PS_SOLID, std::max(1, config_.layout.cardBorderWidth), PanelBorderColor());
    HBRUSH fill = CreateSolidBrush(PanelFillColor());
    HGDIOBJ oldPen = SelectObject(hdc, border);
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    const int radius = std::max(1, config_.layout.cardRadius);
    RoundRect(hdc, card.rect.left, card.rect.top, card.rect.right, card.rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(fill);
    DeleteObject(border);
    DrawPanelIcon(hdc, card.iconName, card.iconRect);
    DrawTextBlock(hdc, card.titleRect, card.title, fonts_.title, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}

bool DashboardApp::MeasureFonts() {
    HDC hdc = GetDC(hwnd_ != nullptr ? hwnd_ : nullptr);
    if (hdc == nullptr) {
        return false;
    }

    const auto measure = [&](HFONT font) {
        TEXTMETRICW metrics{};
        HGDIOBJ oldFont = SelectObject(hdc, font);
        GetTextMetricsW(hdc, &metrics);
        SelectObject(hdc, oldFont);
        return static_cast<int>(metrics.tmHeight);
    };

    fontHeights_.title = measure(fonts_.title);
    fontHeights_.big = measure(fonts_.big);
    fontHeights_.value = measure(fonts_.value);
    fontHeights_.label = measure(fonts_.label);
    fontHeights_.smallText = measure(fonts_.smallFont);
    ReleaseDC(hwnd_ != nullptr ? hwnd_ : nullptr, hdc);
    return true;
}

int DashboardApp::EffectiveHeaderHeight() const {
    const int titleHeight = std::max(fontHeights_.title, config_.layout.headerIconSize);
    return std::max(config_.layout.headerHeight, titleHeight);
}

int DashboardApp::EffectiveMetricRowHeight() const {
    return std::max(config_.layout.metricRowHeight,
        std::max(fontHeights_.label, fontHeights_.value) + std::max(8, config_.layout.widgetLineGap));
}

int DashboardApp::PreferredNodeHeight(const LayoutNodeConfig& node, int width) const {
    const std::string lowered = ToLower(node.name);
    if (lowered == "stack_top") {
        int total = 0;
        for (size_t i = 0; i < node.children.size(); ++i) {
            total += PreferredNodeHeight(node.children[i], width);
            if (i + 1 < node.children.size()) {
                total += config_.layout.widgetLineGap;
            }
        }
        return total;
    }
    if (lowered == "cpu_name" || lowered == "gpu_name") {
        return fontHeights_.label + 2;
    }
    if (lowered == "network_footer") {
        return fontHeights_.smallText + 2;
    }
    if (lowered == "metric_list_cpu" || lowered == "metric_list_gpu") {
        const int count = std::max<int>(1, static_cast<int>(GetNodeParameter(node, "items").has_value()
            ? Split(*GetNodeParameter(node, "items"), ',').size()
            : 4));
        return count * EffectiveMetricRowHeight();
    }
    if (lowered == "drive_usage_list") {
        const int count = std::max<int>(1, static_cast<int>(GetNodeParameter(node, "drives").has_value()
            ? Split(*GetNodeParameter(node, "drives"), ',').size()
            : 3));
        return count * std::max(1, config_.layout.driveRowHeight);
    }
    if (lowered == "throughput_upload" || lowered == "throughput_download" ||
        lowered == "throughput_read" || lowered == "throughput_write") {
        return fontHeights_.smallText + config_.layout.throughputHeaderGap +
            std::max(1, config_.layout.throughputGraphHeight);
    }
    if (lowered == "clock_time") {
        return fontHeights_.big + 8;
    }
    if (lowered == "clock_date") {
        return fontHeights_.value + 6;
    }
    if (lowered == "gauge_cpu_load" || lowered == "gauge_gpu_load") {
        return std::max(1, config_.layout.gaugePreferredSize);
    }
    if (lowered == "spacer") {
        return 0;
    }
    return 0;
}

void DashboardApp::ResolveNodeWidgets(const LayoutNodeConfig& node, const RECT& rect, std::vector<ResolvedWidgetLayout>& widgets) {
    if (!IsContainerNode(node)) {
        ResolvedWidgetLayout widget;
        widget.kind = ParseWidgetKind(node.name);
        widget.rect = rect;
        if (const auto items = GetNodeParameter(node, "items"); items.has_value()) {
            widget.binding.items = Split(*items, ',');
        }
        if (const auto drives = GetNodeParameter(node, "drives"); drives.has_value()) {
            widget.binding.drives = Split(*drives, ',');
        }
        widgets.push_back(std::move(widget));
        return;
    }

    const bool horizontal = ToLower(node.name) == "columns";
    const bool topPacked = ToLower(node.name) == "stack_top";
    const int gap = horizontal ? config_.layout.columnGap : config_.layout.widgetLineGap;
    if (topPacked) {
        int cursor = static_cast<int>(rect.top);
        for (size_t i = 0; i < node.children.size(); ++i) {
            const auto& child = node.children[i];
            int preferred = PreferredNodeHeight(child, static_cast<int>(rect.right - rect.left));
            if (preferred <= 0) {
                preferred = std::max(0, (static_cast<int>(rect.bottom) - cursor) / static_cast<int>(node.children.size() - i));
            }
            if (cursor >= static_cast<int>(rect.bottom)) {
                break;
            }
            RECT childRect{rect.left, cursor, rect.right, std::min(static_cast<int>(rect.bottom), cursor + preferred)};
            ResolveNodeWidgets(child, childRect, widgets);
            cursor = static_cast<int>(childRect.bottom) + gap;
        }
        return;
    }

    int totalWeight = 0;
    for (const auto& child : node.children) {
        totalWeight += std::max(1, child.weight);
    }
    if (totalWeight <= 0) {
        return;
    }

    const int totalAvailable = (horizontal ? (rect.right - rect.left) : (rect.bottom - rect.top)) -
        gap * static_cast<int>(std::max<size_t>(0, node.children.size() - 1));
    int remainingAvailable = totalAvailable;
    int cursor = horizontal ? rect.left : rect.top;
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        const int childWeight = std::max(1, child.weight);
        const int remainingWeight = std::max(1, totalWeight);
        int size = (i + 1 == node.children.size())
            ? ((horizontal ? rect.right : rect.bottom) - cursor)
            : std::max(0, remainingAvailable * childWeight / remainingWeight);

        RECT childRect = rect;
        if (horizontal) {
            childRect.left = cursor;
            childRect.right = cursor + size;
        } else {
            childRect.top = cursor;
            childRect.bottom = cursor + size;
        }

        ResolveNodeWidgets(child, childRect, widgets);
        cursor += size + gap;
        remainingAvailable -= size;
        totalWeight -= childWeight;
    }
}

bool DashboardApp::ResolveLayout() {
    resolvedLayout_ = {};
    resolvedLayout_.windowWidth = WindowWidth();
    resolvedLayout_.windowHeight = WindowHeight();

    const RECT dashboardRect{
        config_.layout.outerMargin,
        config_.layout.outerMargin,
        WindowWidth() - config_.layout.outerMargin,
        WindowHeight() - config_.layout.outerMargin
    };

    int totalRowWeight = 0;
    for (const auto& row : config_.layout.rows) {
        totalRowWeight += std::max(1, row.weight);
    }
    if (totalRowWeight <= 0) {
        return false;
    }

    const int rowGap = config_.layout.rowGap;
    const int totalHeight = (dashboardRect.bottom - dashboardRect.top) -
        rowGap * static_cast<int>(std::max<size_t>(0, config_.layout.rows.size() - 1));
    int remainingHeight = totalHeight;
    int rowTop = dashboardRect.top;
    int remainingRowWeight = totalRowWeight;

    for (size_t rowIndex = 0; rowIndex < config_.layout.rows.size(); ++rowIndex) {
        const auto& row = config_.layout.rows[rowIndex];
        const int rowWeight = std::max(1, row.weight);
        const int rowHeight = (rowIndex + 1 == config_.layout.rows.size())
            ? (dashboardRect.bottom - rowTop)
            : std::max(0, remainingHeight * rowWeight / remainingRowWeight);

        int totalCardWeight = 0;
        for (const auto& cardRef : row.cards) {
            totalCardWeight += std::max(1, cardRef.weight);
        }

        const int cardGap = config_.layout.cardGap;
        const int totalWidth = (dashboardRect.right - dashboardRect.left) -
            cardGap * static_cast<int>(std::max<size_t>(0, row.cards.size() - 1));
        int remainingWidth = totalWidth;
        int cardLeft = dashboardRect.left;
        int remainingCardWeight = std::max(1, totalCardWeight);

        for (size_t cardIndex = 0; cardIndex < row.cards.size(); ++cardIndex) {
            const auto& cardRef = row.cards[cardIndex];
            const auto cardIt = std::find_if(config_.layout.cards.begin(), config_.layout.cards.end(), [&](const auto& card) {
                return ToLower(card.id) == ToLower(cardRef.cardId);
            });
            if (cardIt == config_.layout.cards.end()) {
                continue;
            }

            const int cardWeight = std::max(1, cardRef.weight);
            const int cardWidth = (cardIndex + 1 == row.cards.size())
                ? (dashboardRect.right - cardLeft)
                : std::max(0, remainingWidth * cardWeight / remainingCardWeight);

            ResolvedCardLayout card;
            card.id = cardIt->id;
            card.title = cardIt->title;
            card.iconName = cardIt->icon;
            card.rect = RECT{cardLeft, rowTop, cardLeft + cardWidth, rowTop + rowHeight};

            const int padding = config_.layout.cardPadding;
            const int headerHeight = EffectiveHeaderHeight();
            card.iconRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + std::max(0, (headerHeight - config_.layout.headerIconSize) / 2),
                card.rect.left + padding + config_.layout.headerIconSize,
                card.rect.top + padding + std::max(0, (headerHeight - config_.layout.headerIconSize) / 2) + config_.layout.headerIconSize
            };
            card.titleRect = RECT{
                card.iconRect.right + config_.layout.headerGap,
                card.rect.top + padding,
                card.rect.right - padding,
                card.rect.top + padding + headerHeight
            };
            card.contentRect = RECT{
                card.rect.left + padding,
                card.rect.top + padding + headerHeight + config_.layout.contentGap,
                card.rect.right - padding,
                card.rect.bottom - padding
            };

            ResolveNodeWidgets(cardIt->layout, card.contentRect, card.widgets);
            resolvedLayout_.cards.push_back(std::move(card));

            cardLeft += cardWidth + cardGap;
            remainingWidth -= cardWidth;
            remainingCardWeight -= cardWeight;
        }

        rowTop += rowHeight + rowGap;
        remainingHeight -= rowHeight;
        remainingRowWeight -= rowWeight;
    }

    return !resolvedLayout_.cards.empty();
}

POINT DashboardApp::PolarPoint(int cx, int cy, int radius, double angleDegrees) {
    const double radians = angleDegrees * 3.14159265358979323846 / 180.0;
    return POINT{
        cx + static_cast<LONG>(std::round(std::cos(radians) * radius)),
        cy - static_cast<LONG>(std::round(std::sin(radians) * radius))
    };
}

void DashboardApp::DrawGauge(HDC hdc, int cx, int cy, int radius, double percent, const std::string& label) {
    HPEN trackPen = CreatePen(PS_SOLID, 10, TrackColor());
    HPEN usagePen = CreatePen(PS_SOLID, 10, UsageFillColor());
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
    DrawTextBlock(hdc, numberRect, number, fonts_.big, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    RECT labelRect{cx - 42, cy + 18, cx + 42, cy + 42};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

void DashboardApp::DrawMetricRow(
    HDC hdc, const RECT& rect, const std::string& label, const std::string& value, double ratio) {
    const int labelWidth = std::max(1, config_.layout.metricLabelWidth);
    const int valueGap = std::max(0, config_.layout.metricValueGap);
    RECT labelRect{rect.left, rect.top, std::min(rect.right, rect.left + labelWidth), rect.bottom};
    RECT valueRect{std::min(rect.right, labelRect.right + valueGap), rect.top, rect.right, rect.bottom};
    DrawTextBlock(hdc, labelRect, label, fonts_.label, MutedTextColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, valueRect, value, fonts_.value, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const int metricBarHeight = std::max(1, config_.layout.metricBarHeight);
    const int barBottom = std::min(static_cast<int>(rect.bottom), static_cast<int>(rect.top) + EffectiveMetricRowHeight());
    const int barTop = std::max(static_cast<int>(rect.top), barBottom - metricBarHeight);
    RECT barRect{valueRect.left, barTop, rect.right, barBottom};
    HBRUSH track = CreateSolidBrush(TrackColor());
    FillRect(hdc, &barRect, track);
    DeleteObject(track);

    RECT fill = barRect;
    fill.right = fill.left + static_cast<int>((fill.right - fill.left) * std::clamp(ratio, 0.0, 1.0));
    HBRUSH accent = CreateSolidBrush(UsageFillColor());
    FillRect(hdc, &fill, accent);
    DeleteObject(accent);
}

void DashboardApp::DrawGraph(HDC hdc, const RECT& rect, const std::vector<double>& history, double maxValue) {
    HBRUSH bg = CreateSolidBrush(GraphBackgroundColor());
    FillRect(hdc, &rect, bg);
    DeleteObject(bg);

    const int axisWidth = std::max(1, config_.layout.throughputAxisWidth);
    const int graphLeft = rect.left + axisWidth;
    const int width = std::max<int>(1, rect.right - graphLeft - 1);
    const int height = std::max<int>(1, rect.bottom - rect.top - 1);
    const int graphRight = graphLeft + width;
    const int graphBottom = rect.bottom - 1;

    HPEN gridPen = CreatePen(PS_SOLID, 1, GraphGridColor());
    HGDIOBJ oldPen = SelectObject(hdc, gridPen);
    for (double tick = 5.0; tick < maxValue; tick += 5.0) {
        const double ratio = tick / maxValue;
        const int y = graphBottom - static_cast<int>(std::round(ratio * height));
        MoveToEx(hdc, graphLeft, y, nullptr);
        LineTo(hdc, graphRight, y);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(gridPen);

    HPEN axisPen = CreatePen(PS_SOLID, 1, GraphAxisColor());
    oldPen = SelectObject(hdc, axisPen);
    MoveToEx(hdc, rect.left + axisWidth, rect.top, nullptr);
    LineTo(hdc, rect.left + axisWidth, rect.bottom - 1);
    MoveToEx(hdc, rect.left + axisWidth, rect.bottom - 1, nullptr);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(axisPen);

    char maxLabel[32];
    sprintf_s(maxLabel, "%.0f", maxValue);
    RECT maxRect{rect.left, rect.top + 1, rect.left + axisWidth, rect.top + 13};
    DrawTextBlock(hdc, maxRect, maxLabel, fonts_.smallFont, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_TOP);

    HPEN pen = CreatePen(PS_SOLID, 2, AccentColor());
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

void DashboardApp::DrawThroughputWidget(HDC hdc, const RECT& rect, const char* label,
    double valueMbps, const std::vector<double>& history, double maxGraph) {
    const int lineHeight = fontHeights_.smallText + 2;
    RECT valueRect{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + lineHeight)};
    RECT graphRect{rect.left, std::min(rect.bottom, valueRect.bottom + std::max(0, config_.layout.throughputHeaderGap)), rect.right, rect.bottom};
    const int labelWidth = strcmp(label, "Write") == 0
        ? std::max(1, config_.layout.throughputWriteLabelWidth)
        : std::max(1, config_.layout.throughputReadLabelWidth);
    RECT labelRect{valueRect.left, valueRect.top, std::min(valueRect.right, valueRect.left + labelWidth), valueRect.bottom};
    RECT numberRect{std::min(valueRect.right, labelRect.right + std::max(0, config_.layout.throughputHeaderGap)), valueRect.top, valueRect.right, valueRect.bottom};
    DrawTextBlock(hdc, labelRect, label, fonts_.smallFont, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, numberRect, FormatSpeed(valueMbps), fonts_.smallFont, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DrawGraph(hdc, graphRect, history, maxGraph);
}

void DashboardApp::DrawMetricListWidget(HDC hdc, const RECT& rect, const std::vector<std::string>& items,
    bool isGpu, const SystemSnapshot& snapshot) {
    const int rowHeight = EffectiveMetricRowHeight();
    RECT row{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + rowHeight)};
    for (const auto& item : items) {
        const std::string lowered = ToLower(item);
        std::string label;
        std::string value;
        double ratio = 0.0;
        if (!isGpu && lowered == "temp") {
            label = "Temp";
            value = FormatValue(snapshot.cpu.temperature, 0);
            ratio = snapshot.cpu.temperature.value.value_or(0.0) / 100.0;
        } else if (!isGpu && lowered == "clock") {
            label = "Clock";
            value = FormatValue(snapshot.cpu.clock, 2);
            ratio = snapshot.cpu.clock.value.value_or(0.0) / 5.0;
        } else if (!isGpu && lowered == "fan") {
            label = "Fan";
            value = FormatValue(snapshot.cpu.fan, 0);
            ratio = snapshot.cpu.fan.value.value_or(0.0) / 3000.0;
        } else if (!isGpu && lowered == "ram") {
            label = "RAM";
            value = FormatMemory(snapshot.cpu.memory.usedGb, snapshot.cpu.memory.totalGb);
            ratio = snapshot.cpu.memory.totalGb > 0.0 ? snapshot.cpu.memory.usedGb / snapshot.cpu.memory.totalGb : 0.0;
        } else if (isGpu && lowered == "temp") {
            label = "Temp";
            value = FormatValue(snapshot.gpu.temperature, 0);
            ratio = snapshot.gpu.temperature.value.value_or(0.0) / 100.0;
        } else if (isGpu && lowered == "clock") {
            label = "Clock";
            value = FormatValue(snapshot.gpu.clock, 0);
            ratio = snapshot.gpu.clock.value.value_or(0.0) / 2600.0;
        } else if (isGpu && lowered == "fan") {
            label = "Fan";
            value = FormatValue(snapshot.gpu.fan, 0);
            ratio = snapshot.gpu.fan.value.value_or(0.0) / 3000.0;
        } else if (isGpu && lowered == "vram") {
            label = "VRAM";
            value = FormatMemory(snapshot.gpu.vram.usedGb, std::max(1.0, snapshot.gpu.vram.totalGb));
            ratio = snapshot.gpu.vram.totalGb > 0.0 ? snapshot.gpu.vram.usedGb / snapshot.gpu.vram.totalGb : 0.0;
        } else {
            continue;
        }
        DrawMetricRow(hdc, row, label, value, ratio);
        OffsetRect(&row, 0, rowHeight);
        row.bottom = std::min(rect.bottom, row.top + rowHeight);
        if (row.top >= rect.bottom) {
            break;
        }
    }
}

void DashboardApp::DrawDriveUsageWidget(HDC hdc, const RECT& rect, const std::vector<std::string>& drives,
    const std::vector<DriveInfo>& availableDrives) {
    std::vector<DriveInfo> ordered;
    for (const auto& driveName : drives) {
        const auto it = std::find_if(availableDrives.begin(), availableDrives.end(), [&](const DriveInfo& drive) {
            return ToLower(drive.label) == ToLower(driveName + ":") || ToLower(drive.label) == ToLower(driveName);
        });
        if (it != availableDrives.end()) {
            ordered.push_back(*it);
        }
    }
    if (ordered.empty()) {
        ordered = availableDrives;
    }
    if (ordered.empty()) {
        return;
    }

    const int configuredDriveRowHeight = std::max(1, config_.layout.driveRowHeight);
    const int rowHeight = configuredDriveRowHeight;
    RECT row{rect.left, rect.top, rect.right, std::min(rect.bottom, rect.top + rowHeight)};
    for (const auto& drive : ordered) {
        const int labelWidth = std::max(1, config_.layout.driveLabelWidth);
        const int percentWidth = std::max(1, config_.layout.drivePercentWidth);
        const int freeWidth = std::max(1, config_.layout.driveFreeWidth);
        const int barGap = std::max(0, config_.layout.driveBarGap);
        const int valueGap = std::max(0, config_.layout.driveValueGap);
        RECT labelRect{row.left, row.top, std::min(row.right, row.left + labelWidth), row.bottom};
        RECT pctRect{std::max(row.left, row.right - (percentWidth + freeWidth + valueGap)), row.top,
            std::max(row.left, row.right - (freeWidth + valueGap)), row.bottom};
        RECT freeRect{std::max(row.left, row.right - freeWidth), row.top, row.right, row.bottom};
        const int driveBarHeight = std::max(2, config_.layout.driveBarHeight);
        const int rowPixelHeight = static_cast<int>(row.bottom - row.top);
        const int barTop = static_cast<int>(row.top) + std::max(0, (rowPixelHeight - driveBarHeight) / 2);
        RECT barRect{
            labelRect.right + barGap,
            barTop,
            std::max(static_cast<int>(labelRect.right) + barGap, static_cast<int>(pctRect.left) - valueGap),
            std::min(static_cast<int>(row.bottom), barTop + driveBarHeight)
        };

        DrawTextBlock(hdc, labelRect, drive.label, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        HBRUSH track = CreateSolidBrush(TrackColor());
        FillRect(hdc, &barRect, track);
        DeleteObject(track);

        RECT fill = barRect;
        fill.right = fill.left + static_cast<int>((fill.right - fill.left) * std::clamp(drive.usedPercent / 100.0, 0.0, 1.0));
        HBRUSH accent = CreateSolidBrush(UsageFillColor());
        FillRect(hdc, &fill, accent);
        DeleteObject(accent);

        char percent[16];
        sprintf_s(percent, "%.0f%%", drive.usedPercent);
        DrawTextBlock(hdc, pctRect, percent, fonts_.label, ForegroundColor(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        DrawTextBlock(hdc, freeRect, FormatDriveFree(drive.freeGb), fonts_.smallFont, MutedTextColor(),
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

        OffsetRect(&row, 0, rowHeight);
        row.bottom = std::min(rect.bottom, row.top + rowHeight);
        if (row.top >= rect.bottom) {
            break;
        }
    }
}

void DashboardApp::DrawResolvedWidget(HDC hdc, const ResolvedWidgetLayout& widget, const SystemSnapshot& snapshot) {
    switch (widget.kind) {
    case WidgetKind::CpuName:
        DrawTextBlock(hdc, widget.rect, snapshot.cpu.name, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    case WidgetKind::GpuName:
        DrawTextBlock(hdc, widget.rect, snapshot.gpu.name, fonts_.label, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    case WidgetKind::GaugeCpuLoad:
    case WidgetKind::GaugeGpuLoad: {
        const double percent = widget.kind == WidgetKind::GaugeCpuLoad ? snapshot.cpu.loadPercent : snapshot.gpu.loadPercent;
        const int width = widget.rect.right - widget.rect.left;
        const int height = widget.rect.bottom - widget.rect.top;
        const int radius = std::max(20, std::min(width, height) / 3);
        DrawGauge(hdc, widget.rect.left + width / 2, widget.rect.top + height / 2, radius, percent, "Load");
        return;
    }
    case WidgetKind::MetricListCpu:
        DrawMetricListWidget(hdc, widget.rect, widget.binding.items, false, snapshot);
        return;
    case WidgetKind::MetricListGpu:
        DrawMetricListWidget(hdc, widget.rect, widget.binding.items, true, snapshot);
        return;
    case WidgetKind::ThroughputUpload:
        DrawThroughputWidget(hdc, widget.rect, "Up", snapshot.network.uploadMbps, snapshot.network.uploadHistory,
            GetThroughputGraphMax(snapshot.network.uploadHistory, snapshot.network.downloadHistory));
        return;
    case WidgetKind::ThroughputDownload:
        DrawThroughputWidget(hdc, widget.rect, "Down", snapshot.network.downloadMbps, snapshot.network.downloadHistory,
            GetThroughputGraphMax(snapshot.network.uploadHistory, snapshot.network.downloadHistory));
        return;
    case WidgetKind::ThroughputRead:
        DrawThroughputWidget(hdc, widget.rect, "Read", snapshot.storage.readMbps, snapshot.storage.readHistory,
            GetThroughputGraphMax(snapshot.storage.readHistory, snapshot.storage.writeHistory));
        return;
    case WidgetKind::ThroughputWrite:
        DrawThroughputWidget(hdc, widget.rect, "Write", snapshot.storage.writeMbps, snapshot.storage.writeHistory,
            GetThroughputGraphMax(snapshot.storage.readHistory, snapshot.storage.writeHistory));
        return;
    case WidgetKind::NetworkFooter: {
        const std::string footer = snapshot.network.adapterName.empty()
            ? snapshot.network.ipAddress
            : snapshot.network.adapterName + " | " + snapshot.network.ipAddress;
        DrawTextBlock(hdc, widget.rect, footer, fonts_.smallFont, ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    case WidgetKind::Spacer:
        return;
    case WidgetKind::DriveUsageList:
        DrawDriveUsageWidget(hdc, widget.rect, widget.binding.drives, snapshot.drives);
        return;
    case WidgetKind::ClockTime: {
        char timeBuffer[32];
        sprintf_s(timeBuffer, "%02d:%02d", snapshot.now.wHour, snapshot.now.wMinute);
        DrawTextBlock(hdc, widget.rect, timeBuffer, fonts_.big, ForegroundColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }
    case WidgetKind::ClockDate: {
        char dateBuffer[32];
        sprintf_s(dateBuffer, "%04d-%02d-%02d", snapshot.now.wYear, snapshot.now.wMonth, snapshot.now.wDay);
        DrawTextBlock(hdc, widget.rect, dateBuffer, fonts_.value, MutedTextColor(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }
    default:
        return;
    }
}

void DashboardApp::DrawLayout(HDC hdc, const SystemSnapshot& snapshot) {
    for (const auto& card : resolvedLayout_.cards) {
        DrawPanel(hdc, card);
        for (const auto& widget : card.widgets) {
            DrawResolvedWidget(hdc, widget, snapshot);
        }
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const DiagnosticsOptions diagnosticsOptions = GetDiagnosticsOptions();

    if (const auto elevatedSaveSource = GetSwitchValue(L"/save-config"); elevatedSaveSource.has_value()) {
        const auto elevatedSaveTarget = GetSwitchValue(L"/save-config-target");
        return RunElevatedSaveConfigMode(*elevatedSaveSource, elevatedSaveTarget.value_or(std::filesystem::path{}));
    }

    if (diagnosticsOptions.exit) {
        return RunDiagnosticsHeadlessMode(diagnosticsOptions);
    }

    ShutdownPreviousInstance();

    DashboardApp app(diagnosticsOptions);
    if (!app.Initialize(instance)) {
        MessageBoxW(nullptr, L"Failed to initialize the telemetry dashboard.", L"System Telemetry", MB_ICONERROR);
        return 1;
    }
    return app.Run();
}
