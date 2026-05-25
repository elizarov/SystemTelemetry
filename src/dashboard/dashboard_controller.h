#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics/diagnostics.h"
#include "display/monitor.h"
#include "layout_edit/layout_edit_controller.h"
#include "util/file_path.h"
#include "util/scale.h"

struct DashboardSessionState {
    AppConfig config;
    std::unique_ptr<TelemetryRuntime> telemetry;
    TelemetryUpdate telemetryUpdate;
    std::unique_ptr<DiagnosticsSession> diagnostics;
    std::chrono::steady_clock::time_point lastDiagnosticsOutput{};
    std::optional<DisplayConfig> committedDisplayConfig;
    std::optional<AppConfig> committedWallpaperOwnerConfig;
    UINT currentDpi = kDefaultDpi;
    bool placementWatchActive = false;
    bool isMoving = false;
    bool isEditingLayout = false;
    bool hasUnsavedLayoutEditChanges = false;
    // Size: allocate the saved layout only during edit mode; an always-live second LayoutConfig measured larger.
    std::unique_ptr<LayoutConfig> layoutEditSessionSavedLayout;
    std::string lastError;
};

class DashboardShellHost : public TelemetryUpdateSink {
public:
    virtual ~DashboardShellHost() = default;

    void OnTelemetryUpdate(const TelemetryUpdate& update) override {
        EnqueueTelemetryUpdate(update);
    }

    virtual HWND WindowHandle() const = 0;
    virtual Trace& TraceLog() = 0;
    virtual DashboardRenderer& Renderer() = 0;
    virtual const DashboardRenderer& Renderer() const = 0;
    virtual DashboardOverlayState& RendererDashboardOverlayState() = 0;
    virtual const DashboardOverlayState& RendererDashboardOverlayState() const = 0;
    virtual UINT CurrentWindowDpi() const = 0;
    virtual double CurrentRenderScale() const = 0;
    virtual bool InitializeFonts() = 0;
    virtual void ReleaseFonts() = 0;
    virtual void ApplyConfigPlacement() = 0;
    virtual void RefreshThemedIcons() = 0;
    virtual void InvalidateShell() = 0;
    virtual void RedrawShellNow() = 0;
    // May be called from the telemetry worker thread. Implementations must only copy the update into a thread-safe
    // handoff and marshal any UI work back to the UI thread.
    virtual void EnqueueTelemetryUpdate(const TelemetryUpdate& update) = 0;
    virtual MonitorPlacementInfo GetWindowPlacementInfo() const = 0;
    virtual MonitorPlacementInfo GetWindowPlacementInfoForScale(double scale) const = 0;
    virtual std::optional<FilePath> PromptDiagnosticsSavePath(
        std::string_view defaultFileName, std::string_view filter, std::string_view defaultExtension) const = 0;
    virtual void ShowError(std::string_view message) const = 0;
};

class DashboardController {
public:
    DashboardController();

    DashboardSessionState& State();
    const DashboardSessionState& State() const;
    bool InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions);
    bool HandleTelemetryUpdate(DashboardShellHost& shell, const TelemetryUpdate& update);
    bool WriteDiagnosticsOutputs(DashboardShellHost& shell);
    void SaveDumpAs(DashboardShellHost& shell);
    void SaveScreenshotAs(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions);
    void SaveFullConfigAs(DashboardShellHost& shell);
    bool IsAutoStartEnabled() const;
    void ToggleAutoStart(DashboardShellHost& shell);
    bool ConfigureDisplay(DashboardShellHost& shell, const DisplayMenuOption& option);
    bool SwitchLayout(DashboardShellHost& shell, const std::string& layoutName, bool diagnosticsEditLayout);
    bool SwitchTheme(DashboardShellHost& shell, const std::string& themeName, bool diagnosticsEditLayout);
    bool SetDisplayScale(DashboardShellHost& shell, double scale);
    void SelectGpuAdapter(DashboardShellHost& shell, const std::string& adapterName);
    void SelectNetworkAdapter(DashboardShellHost& shell, const std::string& adapterName);
    void ToggleStorageDrive(DashboardShellHost& shell, const std::string& driveLetter);
    void RefreshTelemetrySelections(DashboardShellHost& shell);
    void StartLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller);
    void StopLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller, bool diagnosticsEditLayout);
    bool HasUnsavedLayoutEditChanges() const;
    bool RestoreLayoutEditSessionSavedLayout(DashboardShellHost& shell);
    bool ApplyLayoutGuideWeights(
        DashboardShellHost& shell, const LayoutEditLayoutTarget& target, const std::vector<int>& weights);
    bool ApplyLayoutGuideAdjacentWeights(DashboardShellHost& shell,
        const LayoutEditLayoutTarget& target,
        size_t separatorIndex,
        int firstWeight,
        int secondWeight);
    bool ApplyMetricListOrder(
        DashboardShellHost& shell, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs);
    bool ApplyContainerChildOrder(
        DashboardShellHost& shell, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex);
    bool ApplyLayoutEditValue(
        DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, double value);
    bool ApplyLayoutEditFont(
        DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, const UiFontConfig& value);
    bool ApplyLayoutEditFontFamily(DashboardShellHost& shell, const std::string& family);
    bool ApplyLayoutEditFontSet(DashboardShellHost& shell, const FontsConfig& fonts);
    bool ApplyLayoutEditColor(
        DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, unsigned int value);
    bool ApplyLayoutEditColorExpression(
        DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, const std::string& expression);
    bool ApplyLayoutEditTheme(DashboardShellHost& shell, const std::string& themeName);
    bool ApplyLayoutEditThemeColor(DashboardShellHost& shell, const ThemeColorEditKey& key, unsigned int value);
    bool ApplyLayoutEditCardTitle(
        DashboardShellHost& shell, const LayoutCardTitleEditKey& key, const std::string& title);
    void ApplyConfigSnapshot(DashboardShellHost& shell, const AppConfig& config);
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
        const LayoutEditLayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis);
    AppConfig BuildCurrentConfigForSaving() const;
    void UpdateConfigFromMovePlacement(DashboardShellHost& shell);
    void UpdateConfigFromResizePlacement(DashboardShellHost& shell);
    bool SaveCurrentConfig(DashboardShellHost& shell);
    bool ApplyConfiguredWallpaper(Trace& trace);

private:
    void BeginLayoutEditSessionTracking();
    void ClearLayoutEditSessionTracking();
    void RefreshLayoutEditSessionDirtyFlag();
    void MarkLayoutEditSessionSaved();
    void SyncRenderer(DashboardShellHost& shell, bool showLayoutEditGuides, bool refreshThemedIcons = true);
    __declspec(noinline) bool FinishConfigMutation(DashboardShellHost& shell, bool refreshThemedIcons = true);
    void RefreshCommittedDisplayConfig(const AppConfig& config);
    void RefreshCommittedWallpaperOwner(const AppConfig& config);
    std::optional<AppConfig> CommittedWallpaperConfigToClear(const AppConfig& nextConfig) const;
    bool CommitDisplayWallpaperTransition(const AppConfig& nextConfig, Trace& trace, bool applyNextWallpaper);

    DashboardSessionState state_{};
};
