#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include "app_constants.h"
#include "config/config.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "diagnostics/diagnostics_options.h"
#include "telemetry/snapshot_dump.h"
#include "util/trace.h"
#include "util/utf8.h"

bool SaveDumpScreenshot(const std::filesystem::path& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    DashboardRenderer::RenderMode renderMode,
    bool showLayoutEditGuides,
    LayoutSimilarityIndicatorMode similarityIndicatorMode,
    const std::string& editLayoutWidgetName,
    std::optional<RenderPoint> hoverPoint = std::nullopt,
    std::ostream* traceStream = nullptr,
    std::string* errorText = nullptr);

DiagnosticsOptions GetDiagnosticsOptions();
bool ValidateDiagnosticsOptions(const DiagnosticsOptions& options);
DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options);
LayoutSimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options);
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
std::filesystem::path ResolveDiagnosticsOutputPath(const std::filesystem::path& workingDirectory,
    const std::filesystem::path& configuredPath,
    const wchar_t* defaultFileName);
std::optional<std::filesystem::path> PromptSavePath(HWND owner,
    const std::filesystem::path& initialDirectory,
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension);
bool CanWriteRuntimeConfig(const std::filesystem::path& path);
std::filesystem::path CreateTempFilePath(const wchar_t* prefix);
std::filesystem::path CreateElevatedSaveConfigTempPath();
int RunElevatedSaveConfigMode(const std::filesystem::path& sourcePath, const std::filesystem::path& targetPath);

std::unique_ptr<TelemetryCollector> InitializeTelemetryCollectorInstance(
    const AppConfig& runtimeConfig, const DiagnosticsOptions& diagnosticsOptions, std::ostream* traceStream);
bool ReloadTelemetryCollectorFromDisk(const std::filesystem::path& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryCollector>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    DiagnosticsSession* diagnostics);
