#pragma once

#include <memory>

#include "app_platform.h"
#include "layout_edit_service.h"

class IConfigPersistenceService {
public:
    virtual ~IConfigPersistenceService() = default;
    virtual AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) const = 0;
    virtual bool SaveRuntimeConfig(const std::filesystem::path& path, const AppConfig& config, HWND owner) const = 0;
    virtual bool SaveFullConfig(const std::filesystem::path& path, const AppConfig& config) const = 0;
};

class IDiagnosticsService {
public:
    virtual ~IDiagnosticsService() = default;
    virtual std::unique_ptr<DiagnosticsSession> CreateSession(const DiagnosticsOptions& options) const = 0;
    virtual bool WriteOutputs(DiagnosticsSession* session, const TelemetryDump& dump, const AppConfig& config) const = 0;
    virtual bool ReloadTelemetryRuntime(const std::filesystem::path& configPath, AppConfig& activeConfig,
        std::unique_ptr<TelemetryRuntime>& telemetry, const DiagnosticsOptions& diagnosticsOptions,
        DiagnosticsSession* diagnostics) const = 0;
};

class IDisplayConfigurationService {
public:
    virtual ~IDisplayConfigurationService() = default;
    virtual bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) const = 0;
    virtual std::vector<DisplayMenuOption> EnumerateDisplayOptions(const AppConfig& config) const = 0;
    virtual std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) const = 0;
    virtual bool ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, UINT targetDpi,
        std::ostream* traceStream, HWND owner) const = 0;
};

class IAutoStartService {
public:
    virtual ~IAutoStartService() = default;
    virtual bool IsEnabled() const = 0;
    virtual bool Update(bool enabled, HWND owner) const = 0;
};

class IDashboardSessionService {
public:
    virtual ~IDashboardSessionService() = default;
    virtual std::unique_ptr<TelemetryRuntime> InitializeRuntime(const AppConfig& config,
        const DiagnosticsOptions& options, std::ostream* traceStream) const = 0;
    virtual void UpdateSnapshot(TelemetryRuntime& telemetry) const = 0;
    virtual void SetPreferredNetworkAdapter(TelemetryRuntime& telemetry, const std::string& adapterName) const = 0;
    virtual void SetSelectedStorageDrives(TelemetryRuntime& telemetry, const std::vector<std::string>& driveLetters) const = 0;
};

class ILayoutEditingService {
public:
    virtual ~ILayoutEditingService() = default;
    virtual bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) const = 0;
    virtual bool ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) const = 0;
    virtual std::optional<int> EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer, const AppConfig& baseConfig,
        const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) const = 0;
};

class DefaultConfigPersistenceService : public IConfigPersistenceService {
public:
    AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options) const override;
    bool SaveRuntimeConfig(const std::filesystem::path& path, const AppConfig& config, HWND owner) const override;
    bool SaveFullConfig(const std::filesystem::path& path, const AppConfig& config) const override;
};

class DefaultDiagnosticsService : public IDiagnosticsService {
public:
    std::unique_ptr<DiagnosticsSession> CreateSession(const DiagnosticsOptions& options) const override;
    bool WriteOutputs(DiagnosticsSession* session, const TelemetryDump& dump, const AppConfig& config) const override;
    bool ReloadTelemetryRuntime(const std::filesystem::path& configPath, AppConfig& activeConfig,
        std::unique_ptr<TelemetryRuntime>& telemetry, const DiagnosticsOptions& diagnosticsOptions,
        DiagnosticsSession* diagnostics) const override;
};

class DefaultDisplayConfigurationService : public IDisplayConfigurationService {
public:
    bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) const override;
    std::vector<DisplayMenuOption> EnumerateDisplayOptions(const AppConfig& config) const override;
    std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) const override;
    bool ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, UINT targetDpi,
        std::ostream* traceStream, HWND owner) const override;
};

class DefaultAutoStartService : public IAutoStartService {
public:
    bool IsEnabled() const override;
    bool Update(bool enabled, HWND owner) const override;
};

class DefaultDashboardSessionService : public IDashboardSessionService {
public:
    std::unique_ptr<TelemetryRuntime> InitializeRuntime(const AppConfig& config,
        const DiagnosticsOptions& options, std::ostream* traceStream) const override;
    void UpdateSnapshot(TelemetryRuntime& telemetry) const override;
    void SetPreferredNetworkAdapter(TelemetryRuntime& telemetry, const std::string& adapterName) const override;
    void SetSelectedStorageDrives(TelemetryRuntime& telemetry, const std::vector<std::string>& driveLetters) const override;
};

class DefaultLayoutEditingService : public ILayoutEditingService {
public:
    bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) const override;
    bool ApplyValue(AppConfig& config, const LayoutEditHost::ValueTarget& target, double value) const override;
    std::optional<int> EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer, const AppConfig& baseConfig,
        const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) const override;
};
