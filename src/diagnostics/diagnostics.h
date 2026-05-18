#pragma once

#include <windows.h>

#include <commdlg.h>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "config/config.h"
#include "config/diagnostics_options.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "diagnostics/snapshot_dump.h"
#include "util/command_line.h"
#include "util/file_path.h"
#include "util/trace.h"

bool SaveDumpScreenshot(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    DashboardRenderer::RenderMode renderMode,
    bool showLayoutEditGuides,
    LayoutSimilarityIndicatorMode similarityIndicatorMode,
    const std::string& editLayoutWidgetName,
    Trace& trace,
    bool hasHoverPoint = false,
    RenderPoint hoverPoint = {},
    std::string* errorText = nullptr);
bool SaveLayoutGuideSheet(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText = nullptr);
bool SaveRenderedAppIcon(
    const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText = nullptr);

DiagnosticsOptions GetDiagnosticsOptions(const CommandLineArguments& commandLine);
bool ValidateDiagnosticsOptions(const DiagnosticsOptions& options);
DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options);
LayoutSimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options);
int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions);

class DiagnosticsSession {
public:
    DiagnosticsSession(const DiagnosticsOptions& options, Trace& trace);
    ~DiagnosticsSession();

    bool Initialize();
    bool ShouldShowDialogs() const;
    void WriteTraceMarker(TracePrefix prefix, const char* text);
    void WriteTraceMarker(TracePrefix prefix, ResourceStringId text);
    void WriteTraceMarker(TracePrefix prefix, const std::string& text);
    void WriteTraceMarkerWithDetail(TracePrefix prefix, ResourceStringId text, std::string_view detail);
    void WriteTraceMarkerFmt(TracePrefix prefix, const char* format, ...);
    void WriteTraceMarkerFmt(TracePrefix prefix, ResourceStringId format, ...);
    void WriteTraceMarkerVFmt(TracePrefix prefix, const char* format, va_list args);
    void WriteTraceMarkerVFmt(TracePrefix prefix, ResourceStringId format, va_list args);
    bool WriteOutputs(const TelemetryDump& dump, const AppConfig& config);

private:
    void ReportError(TracePrefix prefix, const std::string& traceText, std::string_view message);
    bool ReportSaveError(ResourceStringId traceEvent,
        const char* messageAction,
        const FilePath& path,
        std::string_view detail = {},
        std::string_view traceSuffix = {});
    void ShowFileOpenError(const char* label, const FilePath& path);

    DiagnosticsOptions options_;
    Trace& trace_;
    FilePath tracePath_;
    FilePath dumpPath_;
    FilePath screenshotPath_;
    FilePath layoutGuideSheetPath_;
    FilePath appIconPath_;
    FilePath saveConfigPath_;
    FilePath saveFullConfigPath_;
    std::FILE* traceFile_ = nullptr;
};

std::optional<double> TryParseScaleValue(const std::string& text);
std::optional<int> TryParseAppIconSizeValue(const std::string& text);
std::optional<double> GetScaleSwitchValue(const CommandLineArguments& commandLine);
std::optional<std::string> GetLayoutSwitchValue(const CommandLineArguments& commandLine);
std::optional<std::string> GetThemeSwitchValue(const CommandLineArguments& commandLine);
bool ApplyDiagnosticsLayoutOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics = nullptr);
bool ApplyDiagnosticsThemeOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics = nullptr);
FilePath ResolveDiagnosticsOutputPath(
    const FilePath& workingDirectory, const FilePath& configuredPath, std::string_view defaultFileName);
std::optional<FilePath> PromptSavePath(HWND owner,
    const FilePath& initialDirectory,
    std::string_view defaultFileName,
    std::string_view filter,
    std::string_view defaultExtension);
int RunElevatedSaveConfigMode(const FilePath& sourcePath, const FilePath& targetPath);
std::string FormatTelemetryInitializeError(std::string_view errorText);

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    TelemetryUpdateSink* callback,
    std::string* errorText = nullptr);
bool ReloadTelemetryCollectorFromDisk(const FilePath& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    DiagnosticsSession* diagnostics,
    TelemetryUpdateSink* callback,
    std::string* errorText = nullptr);
