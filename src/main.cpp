#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../resources/resource.h"
#include "config.h"
#include "dashboard_renderer.h"
#include "snapshot_dump.h"
#include "telemetry.h"
#include "telemetry_runtime.h"
#include "trace.h"
#include "utf8.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT_PTR kMoveTimerId = 2;
constexpr UINT_PTR kPlacementTimerId = 3;
constexpr UINT kRefreshTimerMs = 500;
constexpr UINT kMoveTimerMs = 16;
constexpr UINT kPlacementTimerMs = 2000;
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

SIZE MeasureTextSize(HDC hdc, HFONT font, const std::string& text) {
    SIZE size{};
    const std::wstring wide = WideFromUtf8(text);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    if (!wide.empty()) {
        GetTextExtentPoint32W(hdc, wide.c_str(), static_cast<int>(wide.size()), &size);
    }
    SelectObject(hdc, oldFont);
    return size;
}

int MeasureFontHeight(HDC hdc, HFONT font) {
    TEXTMETRICW metrics{};
    HGDIOBJ oldFont = SelectObject(hdc, font);
    GetTextMetricsW(hdc, &metrics);
    SelectObject(hdc, oldFont);
    return static_cast<int>(metrics.tmHeight);
}

int MeasureWrappedTextHeight(HDC hdc, HFONT font, const std::string& text, int width) {
    RECT rect{0, 0, std::max(1, width), 0};
    const std::wstring wide = WideFromUtf8(text);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    DrawTextW(hdc, wide.c_str(), -1, &rect, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hdc, oldFont);
    return rect.bottom - rect.top;
}

bool ContainsInsensitive(const std::string& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::string::npos;
}

std::filesystem::path GetRuntimeConfigPath();
class DashboardApp;
bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const AppConfig& config,
    double scale, std::ostream* traceStream = nullptr, std::string* errorText = nullptr);
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);

DiagnosticsOptions GetDiagnosticsOptions();

class DiagnosticsSession {
public:
    explicit DiagnosticsSession(const DiagnosticsOptions& options);

    bool Initialize();
    bool ShouldShowDialogs() const;
    std::ostream* TraceStream();
    void WriteTraceMarker(const std::string& text);
    bool WriteOutputs(const TelemetryDump& dump, const AppConfig& config);

private:
    void ReportError(const std::string& traceText, const std::wstring& message);
    void ShowFileOpenError(const char* label, const std::filesystem::path& path);

    DiagnosticsOptions options_;
    std::filesystem::path tracePath_;
    std::filesystem::path dumpPath_;
    std::filesystem::path screenshotPath_;
    std::ofstream traceStream_;
};

std::filesystem::path GetExecutableDirectory() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(modulePath).parent_path();
}

std::filesystem::path ResolveDiagnosticsOutputPath(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& configuredPath,
    const wchar_t* defaultFileName) {
    if (configuredPath.empty()) {
        return executableDirectory / defaultFileName;
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return executableDirectory / configuredPath;
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

std::optional<std::wstring> GetOptionalSwitchValue(const std::wstring& target) {
    const std::vector<std::wstring> arguments = GetCommandLineArguments();
    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::wstring& argument = arguments[i];
        if (_wcsicmp(argument.c_str(), target.c_str()) == 0) {
            if (i + 1 < arguments.size()) {
                const std::wstring& next = arguments[i + 1];
                if (!next.empty() && next[0] != L'/') {
                    return next;
                }
            }
            return std::nullopt;
        }
        if (argument.size() > target.size() &&
            _wcsnicmp(argument.c_str(), target.c_str(), target.size()) == 0 &&
            argument[target.size()] == L':') {
            return argument.substr(target.size() + 1);
        }
    }
    return std::nullopt;
}

std::optional<double> TryParseScaleValue(const std::wstring& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::string narrow = Utf8FromWide(text);
    std::replace(narrow.begin(), narrow.end(), ',', '.');
    char* end = nullptr;
    const double value = std::strtod(narrow.c_str(), &end);
    if (end == narrow.c_str() || end == nullptr || *end != '\0' || !std::isfinite(value) || value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> GetScaleSwitchValue() {
    for (const std::wstring& argument : GetCommandLineArguments()) {
        if (argument.size() > 7 && _wcsnicmp(argument.c_str(), L"/scale:", 7) == 0) {
            return TryParseScaleValue(argument.substr(7));
        }
    }
    if (const auto value = GetSwitchValue(L"/scale"); value.has_value()) {
        return TryParseScaleValue(*value);
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
    options.reload = HasSwitch("/reload");
    if (const auto scale = GetScaleSwitchValue(); scale.has_value()) {
        options.scale = *scale;
    }
    if (const auto tracePath = GetOptionalSwitchValue(L"/trace"); tracePath.has_value()) {
        options.trace = true;
        options.tracePath = *tracePath;
    }
    if (const auto dumpPath = GetOptionalSwitchValue(L"/dump"); dumpPath.has_value()) {
        options.dump = true;
        options.dumpPath = *dumpPath;
    }
    if (const auto screenshotPath = GetOptionalSwitchValue(L"/screenshot"); screenshotPath.has_value()) {
        options.screenshot = true;
        options.screenshotPath = *screenshotPath;
    }
    return options;
}

DiagnosticsSession::DiagnosticsSession(const DiagnosticsOptions& options) : options_(options) {}

bool DiagnosticsSession::Initialize() {
    const std::filesystem::path executableDirectory = GetExecutableDirectory();
    if (options_.trace) {
        tracePath_ = ResolveDiagnosticsOutputPath(executableDirectory, options_.tracePath, L"telemetry_trace.txt");
        traceStream_.open(tracePath_, std::ios::binary | std::ios::app);
        if (!traceStream_.is_open()) {
            ShowFileOpenError("trace file", tracePath_);
            return false;
        }
    }
    if (options_.dump) {
        dumpPath_ = ResolveDiagnosticsOutputPath(executableDirectory, options_.dumpPath, L"telemetry_dump.txt");
    }
    if (options_.screenshot) {
        screenshotPath_ =
            ResolveDiagnosticsOutputPath(executableDirectory, options_.screenshotPath, L"telemetry_screenshot.png");
    }
    return true;
}

bool DiagnosticsSession::ShouldShowDialogs() const {
    return !options_.trace;
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

void DiagnosticsSession::ReportError(const std::string& traceText, const std::wstring& message) {
    WriteTraceMarker(traceText);
    if (ShouldShowDialogs()) {
        MessageBoxW(nullptr, message.c_str(), L"System Telemetry", MB_ICONERROR);
    }
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
            ReportError("diagnostics:dump_write_failed path=\"" + Utf8FromWide(dumpPath_.wstring()) + "\"", message);
            return false;
        }
    }

    std::string screenshotError;
    if (options_.screenshot && !SaveDumpScreenshot(
            screenshotPath_, dump.snapshot, config, options_.exit ? options_.scale : 1.0, TraceStream(), &screenshotError)) {
        const std::wstring message =
            WideFromUtf8("Failed to save screenshot:\n" + Utf8FromWide(screenshotPath_.wstring()));
        std::string traceText = "diagnostics:screenshot_save_failed path=\"" + Utf8FromWide(screenshotPath_.wstring()) + "\"";
        if (!screenshotError.empty()) {
            traceText += " detail=\"" + screenshotError + "\"";
        }
        ReportError(traceText, message);
        return false;
    }

    return true;
}

void DiagnosticsSession::ShowFileOpenError(const char* label, const std::filesystem::path& path) {
    const std::wstring message = WideFromUtf8(
        std::string("Failed to open ") + label + ":\n" + Utf8FromWide(path.wstring()));
    ReportError("diagnostics:file_open_failed label=\"" + std::string(label) + "\" path=\"" +
        Utf8FromWide(path.wstring()) + "\"", message);
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

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(
    const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    std::ostream* traceStream) {
    std::unique_ptr<TelemetryRuntime> runtime =
        CreateTelemetryRuntime(diagnosticsOptions, GetExecutableDirectory());
    if (runtime == nullptr) {
        return nullptr;
    }
    if (!runtime->Initialize(runtimeConfig, traceStream)) {
        return nullptr;
    }
    return runtime;
}

bool ReloadTelemetryRuntimeFromDisk(
    const std::filesystem::path& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    DiagnosticsSession* diagnostics) {
    const AppConfig reloadedConfig = LoadConfig(configPath);
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:reload_config_begin");
    }

    telemetry.reset();
    std::unique_ptr<TelemetryRuntime> reloadedTelemetry = InitializeTelemetryRuntimeInstance(
        reloadedConfig, diagnosticsOptions, diagnostics != nullptr ? diagnostics->TraceStream() : nullptr);
    if (reloadedTelemetry == nullptr) {
        telemetry = InitializeTelemetryRuntimeInstance(
            activeConfig, diagnosticsOptions, diagnostics != nullptr ? diagnostics->TraceStream() : nullptr);
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }

    activeConfig = reloadedConfig;
    telemetry = std::move(reloadedTelemetry);
    telemetry->UpdateSnapshot();
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:reload_config_done");
    }
    return true;
}

int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions) {
    AppConfig config = LoadConfig(GetRuntimeConfigPath());
    DiagnosticsSession diagnostics(diagnosticsOptions);
    if (!diagnostics.Initialize()) {
        return 1;
    }

    diagnostics.WriteTraceMarker("diagnostics:headless_start scale=" + std::to_string(diagnosticsOptions.scale));
    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_begin");

    std::unique_ptr<TelemetryRuntime> telemetry =
        InitializeTelemetryRuntimeInstance(config, diagnosticsOptions, diagnostics.TraceStream());
    if (telemetry == nullptr) {
        diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_failed");
        if (diagnostics.ShouldShowDialogs()) {
            MessageBoxW(nullptr, L"Failed to initialize telemetry collector.", L"System Telemetry", MB_ICONERROR);
        }
        return 1;
    }

    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialized");
    Sleep(1000);
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_begin");
    telemetry->UpdateSnapshot();
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_done");
    if (diagnosticsOptions.reload) {
        if (!ReloadTelemetryRuntimeFromDisk(GetRuntimeConfigPath(), config, telemetry, diagnosticsOptions, &diagnostics)) {
            return 1;
        }
    }
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_begin");
    if (!diagnostics.WriteOutputs(telemetry->Dump(), config)) {
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

constexpr UINT kDefaultDpi = USER_DEFAULT_SCREEN_DPI;

using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);

UINT GetMonitorDpi(HMONITOR monitor) {
    if (monitor == nullptr) {
        return kDefaultDpi;
    }

    static GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
        HMODULE module = LoadLibraryW(L"Shcore.dll");
        if (module == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(module, "GetDpiForMonitor"));
    }();

    if (getDpiForMonitor != nullptr) {
        UINT dpiX = kDefaultDpi;
        UINT dpiY = kDefaultDpi;
        if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
            return dpiX;
        }
    }
    return kDefaultDpi;
}

double ScaleFromDpi(UINT dpi) {
    return static_cast<double>(std::max(kDefaultDpi, dpi)) / static_cast<double>(kDefaultDpi);
}

int ScaleLogicalToPhysical(int logicalValue, UINT dpi) {
    if (logicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(logicalValue) * ScaleFromDpi(dpi)));
}

int ScalePhysicalToLogical(int physicalValue, UINT dpi) {
    if (physicalValue == 0) {
        return 0;
    }
    return static_cast<int>(std::lround(static_cast<double>(physicalValue) / ScaleFromDpi(dpi)));
}

struct MonitorPlacementInfo {
    std::string deviceName;
    std::string monitorName = "Unknown";
    std::string configMonitorName;
    RECT monitorRect{};
    POINT relativePosition{};
    UINT dpi = kDefaultDpi;
};

struct MonitorIdentity {
    std::string displayName;
    std::string configName;
};

struct TargetMonitorInfo {
    RECT rect{};
    UINT dpi = kDefaultDpi;
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

std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) {
    if (requestedName.empty()) {
        return std::nullopt;
    }
    struct SearchContext {
        std::string requestedName;
        std::optional<TargetMonitorInfo> result;
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
                context->result = TargetMonitorInfo{info.rcMonitor, GetMonitorDpi(monitor)};
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
        info.dpi = GetMonitorDpi(monitor);
        info.relativePosition.x = ScalePhysicalToLogical(windowRect.left - monitorInfo.rcMonitor.left, info.dpi);
        info.relativePosition.y = ScalePhysicalToLogical(windowRect.top - monitorInfo.rcMonitor.top, info.dpi);
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
    bool ApplyWindowDpi(UINT dpi, const RECT* suggestedRect = nullptr);
    void UpdateRendererScale(double scale);
    UINT CurrentWindowDpi() const;
    void StartMoveMode();
    void StopMoveMode();
    void UpdateMoveTracking();
    void DrawMoveOverlay(HDC hdc);
    bool CreateTrayIcon();
    void RemoveTrayIcon();
    HICON LoadAppIcon(int width, int height);
    COLORREF BackgroundColor() const;
    COLORREF ForegroundColor() const;
    COLORREF AccentColor() const;
    COLORREF MutedTextColor() const;
    int WindowWidth() const;
    int WindowHeight() const;
    void StartPlacementWatch();
    void StopPlacementWatch();
    void RetryConfigPlacementIfPending();

    void DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
        COLORREF color, UINT format);
    void DrawLayout(HDC hdc, const SystemSnapshot& snapshot);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig config_;
    DashboardRenderer renderer_;
    std::unique_ptr<TelemetryRuntime> telemetry_;
    bool isMoving_ = false;
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
    DiagnosticsOptions diagnosticsOptions_;
    std::unique_ptr<DiagnosticsSession> diagnostics_;
    std::chrono::steady_clock::time_point lastDiagnosticsOutput_{};
    UINT currentDpi_ = kDefaultDpi;
    bool placementWatchActive_ = false;
};

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions) : diagnosticsOptions_(diagnosticsOptions) {}

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    config_ = config;
    renderer_.SetConfig(config);
}

void DashboardApp::UpdateRendererScale(double scale) {
    renderer_.SetRenderScale(scale);
}

UINT DashboardApp::CurrentWindowDpi() const {
    if (hwnd_ != nullptr) {
        return GetDpiForWindow(hwnd_);
    }
    return currentDpi_;
}

int DashboardApp::WindowWidth() const {
    return renderer_.WindowWidth();
}

int DashboardApp::WindowHeight() const {
    return renderer_.WindowHeight();
}

bool DashboardApp::Initialize(HINSTANCE instance) {
    instance_ = instance;
    config_ = LoadConfig(GetRuntimeConfigPath());
    renderer_.SetConfig(config_);
    renderer_.SetTraceOutput(nullptr);
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
        renderer_.SetTraceOutput(diagnostics_->TraceStream());
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
    currentDpi_ = GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    if (const auto monitor = FindTargetMonitor(config_.monitorName); monitor.has_value()) {
        currentDpi_ = monitor->dpi;
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        placement.left = monitor->rect.left + ScaleLogicalToPhysical(config_.positionX, currentDpi_);
        placement.top = monitor->rect.top + ScaleLogicalToPhysical(config_.positionY, currentDpi_);
    } else {
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        placement.left = 100 + ScaleLogicalToPhysical(config_.positionX, currentDpi_);
        placement.top = 100 + ScaleLogicalToPhysical(config_.positionY, currentDpi_);
    }
    placement.right = placement.left + WindowWidth();
    placement.bottom = placement.top + WindowHeight();

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
    UINT targetDpi = hwnd_ != nullptr ? CurrentWindowDpi() : GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    int left = 100 + ScaleLogicalToPhysical(config_.positionX, targetDpi);
    int top = 100 + ScaleLogicalToPhysical(config_.positionY, targetDpi);
    bool monitorResolved = config_.monitorName.empty();
    if (const auto monitor = FindTargetMonitor(config_.monitorName); monitor.has_value()) {
        monitorResolved = true;
        targetDpi = monitor->dpi;
        left = monitor->rect.left + ScaleLogicalToPhysical(config_.positionX, targetDpi);
        top = monitor->rect.top + ScaleLogicalToPhysical(config_.positionY, targetDpi);
    }

    if (!monitorResolved) {
        return;
    }

    const UINT currentDpi = CurrentWindowDpi();
    if (targetDpi != currentDpi) {
        SetWindowPos(hwnd_, HWND_TOP, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if ((CurrentWindowDpi() != targetDpi || currentDpi_ != targetDpi) && !ApplyWindowDpi(targetDpi)) {
        return;
    }
    SetWindowPos(hwnd_, HWND_TOP, left, top, WindowWidth(), WindowHeight(), SWP_NOACTIVATE);
}

void DashboardApp::StartPlacementWatch() {
    if (hwnd_ == nullptr || config_.monitorName.empty()) {
        StopPlacementWatch();
        return;
    }
    SetTimer(hwnd_, kPlacementTimerId, kPlacementTimerMs, nullptr);
    placementWatchActive_ = true;
}

void DashboardApp::StopPlacementWatch() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kPlacementTimerId);
    }
    placementWatchActive_ = false;
}

void DashboardApp::RetryConfigPlacementIfPending() {
    if (!placementWatchActive_ || hwnd_ == nullptr || isMoving_) {
        return;
    }
    if (config_.monitorName.empty() || FindTargetMonitor(config_.monitorName).has_value()) {
        ApplyConfigPlacement();
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        StopPlacementWatch();
    }
}

bool DashboardApp::InitializeFonts() {
    renderer_.SetConfig(config_);
    renderer_.SetTraceOutput(diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr);
    return renderer_.Initialize(hwnd_);
}

void DashboardApp::ReleaseFonts() {
    renderer_.Shutdown();
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

COLORREF DashboardApp::MutedTextColor() const {
    return ToColorRef(config_.layout.mutedTextColor);
}

HICON DashboardApp::LoadAppIcon(int width, int height) {
    return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        width, height, LR_DEFAULTCOLOR));
}

bool DashboardApp::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    renderer_.SetConfig(config_);
    renderer_.SetTraceOutput(diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr);
    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    return renderer_.SaveSnapshotPng(imagePath, snapshot);
}

bool DashboardApp::ApplyWindowDpi(UINT dpi, const RECT* suggestedRect) {
    const UINT targetDpi = std::max(kDefaultDpi, dpi);
    if (currentDpi_ == targetDpi && suggestedRect == nullptr) {
        return true;
    }

    currentDpi_ = targetDpi;
    ReleaseFonts();
    UpdateRendererScale(ScaleFromDpi(currentDpi_));
    if (!InitializeFonts()) {
        return false;
    }

    if (suggestedRect != nullptr) {
        SetWindowPos(hwnd_, nullptr,
            suggestedRect->left,
            suggestedRect->top,
            suggestedRect->right - suggestedRect->left,
            suggestedRect->bottom - suggestedRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
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

bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const AppConfig& config,
    double scale, std::ostream* traceStream, std::string* errorText) {
    DashboardRenderer renderer;
    renderer.SetConfig(config);
    renderer.SetRenderScale(scale);
    renderer.SetTraceOutput(traceStream);
    if (!renderer.Initialize()) {
        if (errorText != nullptr) {
            *errorText = renderer.LastError();
        }
        return false;
    }
    const bool saved = renderer.SaveSnapshotPng(imagePath, snapshot);
    if (!saved && errorText != nullptr) {
        *errorText = renderer.LastError();
    }
    return saved;
}

void DashboardApp::BringOnTop() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
}

bool DashboardApp::ReloadConfigFromDisk() {
    if (!ReloadTelemetryRuntimeFromDisk(GetRuntimeConfigPath(), config_, telemetry_, diagnosticsOptions_, diagnostics_.get())) {
        ReleaseFonts();
        InitializeFonts();
        return false;
    }
    ReleaseFonts();
    if (!InitializeFonts()) {
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    StartPlacementWatch();
    ApplyConfigPlacement();
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
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

    HDC hdc = GetDC(hwnd_);
    int cursorOffset = ScaleLogicalToPhysical(24, CurrentWindowDpi());
    if (hdc != nullptr) {
        cursorOffset = std::max(cursorOffset, MeasureFontHeight(hdc, renderer_.SmallFont()) + ScaleLogicalToPhysical(8, CurrentWindowDpi()));
        ReleaseDC(hwnd_, hdc);
    }

    const int x = cursor.x - (WindowWidth() / 2);
    const int y = cursor.y - cursorOffset;
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
    const int margin = ScaleLogicalToPhysical(16, CurrentWindowDpi());
    const int padding = ScaleLogicalToPhysical(12, CurrentWindowDpi());
    const int lineGap = ScaleLogicalToPhysical(6, CurrentWindowDpi());
    const int cornerRadius = ScaleLogicalToPhysical(14, CurrentWindowDpi());
    const int borderWidth = std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi()));

    char positionText[96];
    sprintf_s(positionText, "Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);
    char scaleText[96];
    const double scale = ScaleFromDpi(movePlacementInfo_.dpi);
    sprintf_s(scaleText, "Scale: %.0f%% (%.2fx)", scale * 100.0, scale);

    const std::string titleText = "Move Mode";
    const std::string monitorText = "Monitor: " + movePlacementInfo_.monitorName;
    const std::string hintText = "Left-click to place. Copy monitor name, scale, and x/y into config.";

    const int titleHeight = MeasureFontHeight(hdc, renderer_.LabelFont());
    const int bodyHeight = MeasureFontHeight(hdc, renderer_.SmallFont());
    const int minContentWidth = ScaleLogicalToPhysical(220, CurrentWindowDpi());
    const int maxContentWidth = std::max(minContentWidth, WindowWidth() - margin * 2 - padding * 2);
    int preferredContentWidth = minContentWidth;
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.LabelFont(), titleText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), monitorText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), positionText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), scaleText).cx));
    const int contentWidth = std::min(maxContentWidth, preferredContentWidth);
    const int hintHeight = MeasureWrappedTextHeight(hdc, renderer_.SmallFont(), hintText, contentWidth);
    const int overlayWidth = contentWidth + padding * 2;
    const int overlayHeight = padding * 2 + titleHeight + lineGap + bodyHeight + lineGap +
        bodyHeight + lineGap + bodyHeight + lineGap + hintHeight;
    RECT overlay{margin, margin, margin + overlayWidth, margin + overlayHeight};

    HBRUSH fill = CreateSolidBrush(BackgroundColor());
    HPEN border = CreatePen(PS_SOLID, borderWidth, AccentColor());
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, border);
    RoundRect(hdc, overlay.left, overlay.top, overlay.right, overlay.bottom, cornerRadius, cornerRadius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(fill);
    DeleteObject(border);

    int y = overlay.top + padding;
    RECT titleRect{overlay.left + padding, y, overlay.right - padding, y + titleHeight};
    y = titleRect.bottom + lineGap;
    RECT monitorRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = monitorRect.bottom + lineGap;
    RECT positionRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = positionRect.bottom + lineGap;
    RECT scaleRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = scaleRect.bottom + lineGap;
    RECT hintRect{overlay.left + padding, y, overlay.right - padding, overlay.bottom - padding};

    DrawTextBlock(hdc, titleRect, titleText, renderer_.LabelFont(), AccentColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, monitorText, renderer_.SmallFont(), ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, scaleRect, scaleText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, hintText, renderer_.SmallFont(),
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
        currentDpi_ = CurrentWindowDpi();
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        if (!InitializeFonts()) {
            return -1;
        }
        StartPlacementWatch();
        ApplyConfigPlacement();
        SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
        CreateTrayIcon();
        return 0;
    case WM_TIMER:
        if (wParam == kMoveTimerId) {
            UpdateMoveTracking();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (wParam == kPlacementTimerId) {
            RetryConfigPlacementIfPending();
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
    case WM_DPICHANGED:
        if (!ApplyWindowDpi(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam))) {
            return -1;
        }
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DISPLAYCHANGE:
        StartPlacementWatch();
        RetryConfigPlacementIfPending();
        if (!ApplyWindowDpi(CurrentWindowDpi())) {
            return -1;
        }
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DEVICECHANGE:
    case WM_SETTINGCHANGE:
        StartPlacementWatch();
        RetryConfigPlacementIfPending();
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, kRefreshTimerId);
        KillTimer(hwnd_, kMoveTimerId);
        KillTimer(hwnd_, kPlacementTimerId);
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

void DashboardApp::DrawLayout(HDC hdc, const SystemSnapshot& snapshot) {
    renderer_.Draw(hdc, snapshot);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
