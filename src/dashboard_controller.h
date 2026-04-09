#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "app_platform.h"
#include "dashboard_services.h"
#include "layout_edit_controller.h"

struct DashboardSessionState {
    AppConfig config;
    std::unique_ptr<TelemetryRuntime> telemetry;
    std::unique_ptr<DiagnosticsSession> diagnostics;
    std::chrono::steady_clock::time_point lastDiagnosticsOutput{};
    UINT currentDpi = kDefaultDpi;
    bool placementWatchActive = false;
    bool isMoving = false;
    bool isEditingLayout = false;
    std::vector<DisplayMenuOption> configDisplayOptions;
    std::vector<LayoutMenuOption> layoutMenuOptions;
    std::vector<NetworkMenuOption> networkMenuOptions;
    std::vector<StorageDriveMenuOption> storageDriveMenuOptions;
};

class DashboardShellHost {
public:
    virtual ~DashboardShellHost() = default;
    virtual HWND WindowHandle() const = 0;
    virtual DashboardRenderer& Renderer() = 0;
    virtual const DashboardRenderer& Renderer() const = 0;
    virtual DashboardRenderer::EditOverlayState& RendererEditOverlayState() = 0;
    virtual const DashboardRenderer::EditOverlayState& RendererEditOverlayState() const = 0;
    virtual UINT CurrentWindowDpi() const = 0;
    virtual bool InitializeFonts() = 0;
    virtual void ReleaseFonts() = 0;
    virtual void ApplyConfigPlacement() = 0;
    virtual void InvalidateShell() = 0;
    virtual MonitorPlacementInfo GetWindowPlacementInfo() const = 0;
    virtual std::optional<std::filesystem::path> PromptDiagnosticsSavePath(
        const wchar_t* defaultFileName,
        const wchar_t* filter,
        const wchar_t* defaultExtension) const = 0;
    virtual void ShowError(const std::wstring& message) const = 0;
};

class IDashboardController {
public:
    virtual ~IDashboardController() = default;
    virtual DashboardSessionState& State() = 0;
    virtual const DashboardSessionState& State() const = 0;
    virtual bool InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) = 0;
    virtual bool HandleRefreshTimer(DashboardShellHost& shell) = 0;
    virtual bool WriteDiagnosticsOutputs() = 0;
    virtual bool ReloadConfigFromDisk(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions, LayoutEditController& controller) = 0;
    virtual void SaveDumpAs(DashboardShellHost& shell) = 0;
    virtual void SaveScreenshotAs(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) = 0;
    virtual void SaveFullConfigAs(DashboardShellHost& shell) = 0;
    virtual bool IsAutoStartEnabled() const = 0;
    virtual void ToggleAutoStart(DashboardShellHost& shell) = 0;
    virtual bool ConfigureDisplay(DashboardShellHost& shell, const DisplayMenuOption& option) = 0;
    virtual bool SwitchLayout(DashboardShellHost& shell, const std::string& layoutName, LayoutEditController& controller, bool diagnosticsEditLayout) = 0;
    virtual void SelectNetworkAdapter(DashboardShellHost& shell, const NetworkMenuOption& option) = 0;
    virtual void ToggleStorageDrive(DashboardShellHost& shell, const StorageDriveMenuOption& option) = 0;
    virtual void StartLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller) = 0;
    virtual void StopLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller, bool diagnosticsEditLayout) = 0;
    virtual bool ApplyLayoutGuideWeights(DashboardShellHost& shell, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) = 0;
    virtual bool ApplyLayoutEditValue(DashboardShellHost& shell, const LayoutEditHost::ValueTarget& target, double value) = 0;
    virtual std::optional<int> EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
        const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) = 0;
    virtual AppConfig BuildCurrentConfigForSaving(DashboardShellHost& shell) const = 0;
    virtual void UpdateConfigFromCurrentPlacement(DashboardShellHost& shell) = 0;
};

class DashboardController : public IDashboardController {
public:
    DashboardController();

    DashboardSessionState& State() override;
    const DashboardSessionState& State() const override;
    bool InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) override;
    bool HandleRefreshTimer(DashboardShellHost& shell) override;
    bool WriteDiagnosticsOutputs() override;
    bool ReloadConfigFromDisk(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions, LayoutEditController& controller) override;
    void SaveDumpAs(DashboardShellHost& shell) override;
    void SaveScreenshotAs(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) override;
    void SaveFullConfigAs(DashboardShellHost& shell) override;
    bool IsAutoStartEnabled() const override;
    void ToggleAutoStart(DashboardShellHost& shell) override;
    bool ConfigureDisplay(DashboardShellHost& shell, const DisplayMenuOption& option) override;
    bool SwitchLayout(DashboardShellHost& shell, const std::string& layoutName, LayoutEditController& controller, bool diagnosticsEditLayout) override;
    void SelectNetworkAdapter(DashboardShellHost& shell, const NetworkMenuOption& option) override;
    void ToggleStorageDrive(DashboardShellHost& shell, const StorageDriveMenuOption& option) override;
    void StartLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller) override;
    void StopLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller, bool diagnosticsEditLayout) override;
    bool ApplyLayoutGuideWeights(DashboardShellHost& shell, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) override;
    bool ApplyLayoutEditValue(DashboardShellHost& shell, const LayoutEditHost::ValueTarget& target, double value) override;
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
        const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) override;
    AppConfig BuildCurrentConfigForSaving(DashboardShellHost& shell) const override;
    void UpdateConfigFromCurrentPlacement(DashboardShellHost& shell) override;

private:
    void SyncRenderer(DashboardShellHost& shell, bool showLayoutEditGuides);
    void SyncRuntimeAndRenderer(DashboardShellHost& shell, bool showLayoutEditGuides);
    bool ApplyConfiguredWallpaper();

    DashboardSessionState state_{};
    DefaultConfigPersistenceService configPersistenceService_{};
    DefaultDiagnosticsService diagnosticsService_{};
    DefaultDisplayConfigurationService displayConfigurationService_{};
    DefaultAutoStartService autoStartService_{};
    DefaultDashboardSessionService dashboardSessionService_{};
    DefaultLayoutEditingService layoutEditingService_{};
};
