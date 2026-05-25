#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "config/diagnostics_options.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "telemetry/telemetry.h"
#include "util/command_line.h"
#include "util/file_path.h"
#include "util/trace.h"

struct AppConfig;

using WriteDiagnosticsExtraOutputsFn = bool (*)(void* context,
    const DiagnosticsOptions& options,
    const TelemetryDump& dump,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText);
using LoadDiagnosticsExtraConfigFn = bool (*)(std::string* configText, std::string* errorText);
using ResolveDiagnosticsExtraConfigFn = void (*)(AppConfig& config);

struct DiagnosticsOutputHandlers {
    void* context = nullptr;
    WriteDiagnosticsExtraOutputsFn writeExtraOutputs = nullptr;
    LoadDiagnosticsExtraConfigFn loadExtraConfig = nullptr;
    ResolveDiagnosticsExtraConfigFn resolveExtraConfig = nullptr;
};

struct DiagnosticsValidationResult {
    bool ok = true;
    std::string reason;
    std::string message;
};

using MarkDiagnosticsCommandLineArgumentFn = void (*)(void* context, size_t argumentIndex);

struct DiagnosticsCommandLineTracker {
    void* context = nullptr;
    MarkDiagnosticsCommandLineArgumentFn markArgument = nullptr;
};

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
bool SaveRenderedAppIcon(
    const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText = nullptr);

DiagnosticsOptions GetDiagnosticsOptions(
    const CommandLineArguments& commandLine, DiagnosticsCommandLineTracker tracker = {});
DiagnosticsValidationResult ValidateDiagnosticsOptions(
    const DiagnosticsOptions& options, DiagnosticsOutputHandlers handlers = {});
void ReportDiagnosticsError(const DiagnosticsOptions& options, std::string_view message);
DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options);
LayoutSimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options);
int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions, DiagnosticsOutputHandlers handlers = {});

class DiagnosticsSession {
public:
    DiagnosticsSession(const DiagnosticsOptions& options, Trace& trace, DiagnosticsOutputHandlers handlers = {});
    ~DiagnosticsSession();

    bool Initialize();
    bool ShouldShowDialogs() const;
    const std::string& LastError() const;
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
    FilePath appIconPath_;
    FilePath saveConfigPath_;
    FilePath saveFullConfigPath_;
    DiagnosticsOutputHandlers handlers_;
    std::string lastError_;
    std::FILE* traceFile_ = nullptr;
};

std::optional<double> TryParseScaleValue(const std::string& text);
std::optional<int> TryParseAppIconSizeValue(const std::string& text);
std::optional<double> GetScaleSwitchValue(const CommandLineArguments& commandLine);
std::optional<std::string> GetLayoutSwitchValue(const CommandLineArguments& commandLine);
std::optional<std::string> GetThemeSwitchValue(const CommandLineArguments& commandLine);
bool ApplyDiagnosticsLayoutOverride(AppConfig& config,
    const DiagnosticsOptions& options,
    DiagnosticsSession* diagnostics = nullptr,
    std::string* errorText = nullptr);
bool ApplyDiagnosticsThemeOverride(AppConfig& config,
    const DiagnosticsOptions& options,
    DiagnosticsSession* diagnostics = nullptr,
    std::string* errorText = nullptr,
    ResolveDiagnosticsExtraConfigFn resolveExtraConfig = nullptr);
FilePath ResolveDiagnosticsOutputPath(
    const FilePath& workingDirectory, const FilePath& configuredPath, std::string_view defaultFileName);
int RunElevatedSaveConfigMode(const FilePath& sourcePath, const FilePath& targetPath);
std::string FormatTelemetryInitializeError(std::string_view errorText);

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    TelemetryUpdateSink* callback,
    std::string* errorText = nullptr);
