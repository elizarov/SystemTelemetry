#pragma once

#include <memory>

#include "app_diagnostics.h"
#include "app_platform.h"
#include "layout_edit_service.h"

class ConfigPersistenceService {
public:
    AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) const;
    bool SaveRuntimeConfig(const std::filesystem::path& path, const AppConfig& config, HWND owner) const;
    bool SaveFullConfig(const std::filesystem::path& path, const AppConfig& config) const;
};

class DiagnosticsService {
public:
    std::unique_ptr<DiagnosticsSession> CreateSession(const DiagnosticsOptions& options) const;
    bool WriteOutputs(DiagnosticsSession* session, const TelemetryDump& dump, const AppConfig& config) const;
    bool ReloadTelemetryRuntime(const std::filesystem::path& configPath, AppConfig& activeConfig,
        std::unique_ptr<TelemetryRuntime>& telemetry, const DiagnosticsOptions& diagnosticsOptions,
        DiagnosticsSession* diagnostics) const;
};

class DisplayConfigurationService {
public:
    bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) const;
    std::vector<DisplayMenuOption> EnumerateDisplayOptions(const AppConfig& config) const;
    std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) const;
    bool ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, UINT targetDpi,
        std::ostream* traceStream, HWND owner) const;
};

class AutoStartService {
public:
    bool IsEnabled() const;
    bool Update(bool enabled, HWND owner) const;
};

class DashboardSessionService {
public:
    std::unique_ptr<TelemetryRuntime> InitializeRuntime(const AppConfig& config,
        const DiagnosticsOptions& options, std::ostream* traceStream) const;
    void UpdateSnapshot(TelemetryRuntime& telemetry) const;
    void SetPreferredNetworkAdapter(TelemetryRuntime& telemetry, const std::string& adapterName) const;
    void SetSelectedStorageDrives(TelemetryRuntime& telemetry, const std::vector<std::string>& driveLetters) const;
};

class LayoutEditingService {
public:
    bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) const;
    bool ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) const;
    std::optional<int> EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer, const AppConfig& baseConfig,
        const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) const;
};
