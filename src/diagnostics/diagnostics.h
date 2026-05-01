#pragma once

#include <windows.h>

#include <commdlg.h>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "diagnostics/diagnostics_options.h"
#include "diagnostics/snapshot_dump.h"
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
    std::optional<RenderPoint> hoverPoint = std::nullopt,
    std::string* errorText = nullptr);
bool SaveLayoutGuideSheet(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText = nullptr);

DiagnosticsOptions GetDiagnosticsOptions();
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
    void WriteTraceMarker(const std::string& text);
    bool WriteOutputs(const TelemetryDump& dump, const AppConfig& config);

private:
    void ReportError(const std::string& traceText, const std::wstring& message);
    void ShowFileOpenError(const char* label, const FilePath& path);

    DiagnosticsOptions options_;
    Trace& trace_;
    FilePath tracePath_;
    FilePath dumpPath_;
    FilePath screenshotPath_;
    FilePath layoutGuideSheetPath_;
    FilePath saveConfigPath_;
    FilePath saveFullConfigPath_;
    std::FILE* traceFile_ = nullptr;
};

std::vector<std::wstring> GetCommandLineArguments();
bool HasSwitch(const std::string& target);
std::optional<std::wstring> GetSwitchValue(const std::wstring& target);
std::optional<std::wstring> GetColonSwitchValue(const std::wstring& target);
std::optional<double> TryParseScaleValue(const std::wstring& text);
std::optional<double> GetScaleSwitchValue();
std::optional<std::string> GetLayoutSwitchValue();
bool ApplyDiagnosticsLayoutOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics = nullptr);
void ApplyDiagnosticsScaleOverride(AppConfig& config, const DiagnosticsOptions& options);
FilePath ResolveDiagnosticsOutputPath(
    const FilePath& workingDirectory, const FilePath& configuredPath, const wchar_t* defaultFileName);
std::optional<FilePath> PromptSavePath(HWND owner,
    const FilePath& initialDirectory,
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension);
bool CanWriteRuntimeConfig(const FilePath& path);
FilePath CreateTempFilePath(const wchar_t* prefix);
FilePath CreateElevatedSaveConfigTempPath();
int RunElevatedSaveConfigMode(const FilePath& sourcePath, const FilePath& targetPath);
std::wstring FormatTelemetryInitializeError(std::string_view errorText);

std::unique_ptr<TelemetryCollector> InitializeTelemetryCollectorInstance(const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    std::string* errorText = nullptr);
bool ReloadTelemetryCollectorFromDisk(const FilePath& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryCollector>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    DiagnosticsSession* diagnostics,
    std::string* errorText = nullptr);
