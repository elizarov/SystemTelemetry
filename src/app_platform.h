#pragma once

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "app_constants.h"
#include "app_types.h"
#include "config.h"
#include "dashboard_renderer.h"
#include "snapshot_dump.h"
#include "telemetry_runtime.h"
#include "trace.h"
#include "utf8.h"

COLORREF ToColorRef(unsigned int color);
std::string ToLower(std::string value);
std::string Trim(std::string value);
SIZE MeasureTextSize(HDC hdc, HFONT font, const std::string& text);
int MeasureFontHeight(HDC hdc, HFONT font);
int MeasureWrappedTextHeight(HDC hdc, HFONT font, const std::string& text, int width);
bool ContainsInsensitive(const std::string& value, const std::string& needle);
bool EqualsInsensitive(const std::string& left, const std::string& right);
bool EqualsInsensitive(const std::wstring& left, const std::wstring& right);
std::string JoinNames(const std::vector<std::string>& names);
bool RectsEqual(const RECT& lhs, const RECT& rhs);
void WriteOptionalTrace(std::ostream* traceStream, const std::string& text);
std::string FormatHresult(HRESULT value);
std::filesystem::path GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options);
bool SaveDumpScreenshot(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot, const AppConfig& config,
    double scale, DashboardRenderer::RenderMode renderMode, bool showLayoutEditGuides,
    DashboardRenderer::SimilarityIndicatorMode similarityIndicatorMode, const std::string& editLayoutWidgetName,
    std::ostream* traceStream = nullptr,
    std::string* errorText = nullptr);
bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);

DiagnosticsOptions GetDiagnosticsOptions();
bool ValidateDiagnosticsOptions(const DiagnosticsOptions& options);
DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options);
DashboardRenderer::SimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options);
bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream);
int RunElevatedConfigureDisplayMode(const std::filesystem::path& sourceConfigPath, const std::filesystem::path& sourceDumpPath,
    const std::filesystem::path& targetConfigPath, const std::filesystem::path& targetImagePath);
int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath);
int RunElevatedAutoStartMode(bool enabled);
int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions);

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
    std::filesystem::path saveConfigPath_;
    std::filesystem::path saveFullConfigPath_;
    std::ofstream traceStream_;
};

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetWorkingDirectory();
std::filesystem::path ResolveExecutableRelativePath(const std::filesystem::path& configuredPath);
std::optional<std::wstring> GetExecutablePath();
std::wstring TrimWhitespace(std::wstring value);
std::wstring StripOuterQuotes(std::wstring value);
std::wstring NormalizeWindowsPath(std::wstring value);
std::wstring QuoteCommandLineArgument(const std::wstring& value);
std::optional<std::wstring> ReadAutoStartCommand();
bool IsAutoStartEnabledForCurrentExecutable();
LSTATUS WriteAutoStartRegistryValue(bool enabled);
bool UpdateAutoStartElevated(bool enabled, HWND owner);
bool UpdateAutoStartRegistration(bool enabled, HWND owner);

std::filesystem::path ResolveDiagnosticsOutputPath(
    const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredPath,
    const wchar_t* defaultFileName);
std::optional<std::filesystem::path> PromptSavePath(
    HWND owner,
    const std::filesystem::path& initialDirectory,
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension);

std::vector<std::wstring> GetCommandLineArguments();
bool HasSwitch(const std::string& target);
std::optional<std::wstring> GetSwitchValue(const std::wstring& target);
std::optional<std::wstring> GetColonSwitchValue(const std::wstring& target);
std::optional<double> TryParseScaleValue(const std::wstring& text);
std::optional<double> GetScaleSwitchValue();
std::optional<std::string> GetLayoutSwitchValue();
bool ApplyDiagnosticsLayoutOverride(AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics = nullptr);

bool CanWriteRuntimeConfig(const std::filesystem::path& path);
std::filesystem::path CreateTempFilePath(const wchar_t* prefix);
std::filesystem::path CreateElevatedSaveConfigTempPath();

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(
    const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    std::ostream* traceStream);
bool ReloadTelemetryRuntimeFromDisk(
    const std::filesystem::path& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    DiagnosticsSession* diagnostics);

std::string FormatSpeed(double mbps);
double GetThroughputGraphMax(const std::vector<double>& firstHistory, const std::vector<double>& secondHistory);
UINT GetMonitorDpi(HMONITOR monitor);
double ScaleFromDpi(UINT dpi);
int ScaleLogicalToPhysical(int logicalValue, UINT dpi);
int ScalePhysicalToLogical(int physicalValue, UINT dpi);
SIZE ComputeWindowSizeForDpi(const AppConfig& config, UINT dpi);

void SetMenuItemRadioStyle(HMENU menu, UINT commandId);
std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress);
std::string FormatStorageDriveSize(double totalGb);
std::string FormatStorageDriveMenuText(const StorageDriveMenuOption& option);
std::string SimplifyDeviceName(const std::string& deviceName);
bool IsUsefulFriendlyName(const std::string& name);
MonitorIdentity GetMonitorIdentity(const std::string& deviceName);
std::vector<DisplayMenuOption> EnumerateDisplayMenuOptions(const AppConfig& config);
std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName);
MonitorPlacementInfo GetMonitorPlacementForWindow(HWND hwnd);
HFONT CreateUiFont(const UiFontConfig& font);
void ShutdownPreviousInstance();
