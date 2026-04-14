#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

#include "app_diagnostics.h"
#include "app_display_config.h"
#include "app_monitor.h"
#include "dashboard_menu_types.h"
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
    std::vector<ScaleMenuOption> scaleMenuOptions;
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
    virtual double CurrentRenderScale() const = 0;
    virtual bool InitializeFonts() = 0;
    virtual void ReleaseFonts() = 0;
    virtual void ApplyConfigPlacement() = 0;
    virtual void InvalidateShell() = 0;
    virtual MonitorPlacementInfo GetWindowPlacementInfo() const = 0;
    virtual std::optional<std::filesystem::path> PromptDiagnosticsSavePath(
        const wchar_t* defaultFileName, const wchar_t* filter, const wchar_t* defaultExtension) const = 0;
    virtual void ShowError(const std::wstring& message) const = 0;
};

class DashboardController {
public:
    DashboardController();

    DashboardSessionState& State();
    const DashboardSessionState& State() const;
    bool InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions);
    bool HandleRefreshTimer(DashboardShellHost& shell);
    bool WriteDiagnosticsOutputs();
    bool ReloadConfigFromDisk(
        DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions, LayoutEditController& controller);
    void SaveDumpAs(DashboardShellHost& shell);
    void SaveScreenshotAs(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions);
    void SaveFullConfigAs(DashboardShellHost& shell);
    bool IsAutoStartEnabled() const;
    void ToggleAutoStart(DashboardShellHost& shell);
    bool ConfigureDisplay(DashboardShellHost& shell, const DisplayMenuOption& option);
    bool SwitchLayout(DashboardShellHost& shell,
        const std::string& layoutName,
        LayoutEditController& controller,
        bool diagnosticsEditLayout);
    bool SetDisplayScale(DashboardShellHost& shell, double scale);
    void SelectNetworkAdapter(DashboardShellHost& shell, const NetworkMenuOption& option);
    void ToggleStorageDrive(DashboardShellHost& shell, const StorageDriveMenuOption& option);
    void RefreshTelemetrySelections(DashboardShellHost& shell);
    void StartLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller);
    void StopLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller, bool diagnosticsEditLayout);
    bool ApplyLayoutGuideWeights(
        DashboardShellHost& shell, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights);
    bool ApplyLayoutEditValue(
        DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, double value);
    bool ApplyLayoutEditFont(
        DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, const UiFontConfig& value);
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
        const LayoutEditHost::LayoutTarget& target,
        const std::vector<int>& weights,
        const DashboardRenderer::LayoutWidgetIdentity& widget,
        DashboardRenderer::LayoutGuideAxis axis);
    AppConfig BuildCurrentConfigForSaving(DashboardShellHost& shell) const;
    void UpdateConfigFromCurrentPlacement(DashboardShellHost& shell);

private:
    void SyncRenderer(DashboardShellHost& shell, bool showLayoutEditGuides);
    void SyncRuntimeAndRenderer(DashboardShellHost& shell, bool showLayoutEditGuides);
    bool ApplyConfiguredWallpaper();

    DashboardSessionState state_{};
};
