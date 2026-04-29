#pragma once

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <shellapi.h>
#include <string>

#include "dashboard/dashboard_controller.h"
#include "diagnostics/diagnostics_options.h"
#include "display/constants.h"
#include "display/monitor.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_trace_session.h"

class DashboardShellUi;

class DashboardApp : private LayoutEditHost, public DashboardShellHost {
public:
    explicit DashboardApp(const DiagnosticsOptions& diagnosticsOptions = {});
    ~DashboardApp();
    bool Initialize(HINSTANCE instance);
    const std::wstring& LastError() const;
    int Run();
    bool InitializeFonts() override;
    void SetRenderConfig(const AppConfig& config);
    void ReleaseFonts() override;
    bool SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot);
    bool WriteDiagnosticsOutputs();
    HWND WindowHandle() const override;
    Trace& TraceLog() override;
    DashboardRenderer& Renderer() override;
    const DashboardRenderer& Renderer() const override;
    DashboardOverlayState& RendererDashboardOverlayState() override;
    const DashboardOverlayState& RendererDashboardOverlayState() const override;
    UINT CurrentWindowDpi() const override;
    double CurrentRenderScale() const override;
    void ApplyConfigPlacement() override;
    void InvalidateShell() override;
    void RedrawShellNow() override;
    MonitorPlacementInfo GetWindowPlacementInfo() const override;
    std::optional<std::filesystem::path> PromptDiagnosticsSavePath(
        const wchar_t* defaultFileName, const wchar_t* filter, const wchar_t* defaultExtension) const override;
    void ShowError(const std::wstring& message) const override;

private:
    friend class DashboardShellUi;

    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void BringOnTop();
    bool ApplyConfiguredWallpaper();
    bool ApplyWindowDpi(UINT dpi, const RECT* suggestedRect = nullptr);
    void UpdateRendererScale(double scale);
    double ResolveCurrentDisplayScale(UINT dpi) const;
    bool IsLayoutEditMode() const;
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis) override;
    void StartMoveMode(std::optional<POINT> cursorAnchorClientPoint = std::nullopt);
    void StopMoveMode();
    void UpdateMoveTracking();
    void SyncDashboardMoveOverlayState();
    bool CreateLayoutEditTooltip();
    void DestroyLayoutEditTooltip();
    void HideLayoutEditTooltip();
    void SetLayoutEditTooltipRefreshSuppressed(bool suppressed);
    void UpdateLayoutEditTooltip();
    void RefreshLayoutEditHoverFromCursor();
    bool ShouldIgnoreCoveredLayoutEditPointer(POINT screenPoint, bool allowDuringDrag) const;
    void SuspendCoveredLayoutEditHover();
    void UpdateLayoutEditMouseTracking();
    void RelayLayoutEditTooltipMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void TraceLayoutEditUiEvent(const std::string& event, const std::string& details = {}) const;
    std::string BuildLayoutEditUiTraceState() const;
    bool CreateTrayIcon();
    void RemoveTrayIcon();
    HICON LoadAppIcon(int width, int height);
    int WindowWidth() const;
    int WindowHeight() const;
    bool HandleRenderEnvironmentChange(const char* reason);
    void RegisterSessionNotifications();
    void UnregisterSessionNotifications();
    void StartPlacementWatch();
    void StopPlacementWatch();
    void RetryConfigPlacementIfPending();

    void BeginLayoutEditTraceSession(const std::string& kind, const std::string& detail) override;
    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override;
    void EndLayoutEditTraceSession(const std::string& reason) override;

    const AppConfig& LayoutEditConfig() const override;
    DashboardOverlayState& LayoutDashboardOverlayState() override;
    LayoutEditActiveRegions CollectLayoutEditActiveRegions() const override;
    LayoutEditHoverResolution ResolveLayoutEditHover(RenderPoint clientPoint) const override;
    double LayoutEditRenderScale() const override;
    int LayoutEditSimilarityThreshold() const override;
    void SetLayoutGuideDragActive(bool active) override;
    void SetLayoutEditInteractiveDragTraceActive(bool active) override;
    void RebuildLayoutEditArtifacts() override;
    bool ApplyLayoutGuideWeights(const LayoutEditLayoutTarget& target, const std::vector<int>& weights) override;
    bool ApplyMetricListOrder(
        const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) override;
    bool ApplyContainerChildOrder(const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) override;
    bool ApplyLayoutEditValue(LayoutEditParameter parameter, double value) override;
    void InvalidateLayoutEdit() override;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Trace trace_;
    DashboardRenderer renderer_;
    DashboardOverlayState rendererDashboardOverlayState_{};
    DashboardController controller_{};
    NOTIFYICONDATAW trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
    DiagnosticsOptions diagnosticsOptions_;
    UINT currentDpi_ = kDefaultDpi;
    LayoutEditController layoutEditController_;
    std::unique_ptr<DashboardShellUi> shellUi_;
    HWND layoutEditTooltipHwnd_ = nullptr;
    std::wstring layoutEditTooltipText_;
    std::wstring lastError_;
    bool layoutEditTooltipVisible_ = false;
    bool layoutEditMouseTracking_ = false;
    RECT layoutEditTooltipRect_{};
    bool layoutEditTooltipRectValid_ = false;
    bool layoutEditTooltipRefreshSuppressed_ = false;
    bool sessionNotificationsRegistered_ = false;
    int layoutEditModalUiDepth_ = 0;
    std::optional<POINT> moveCursorAnchorClientPoint_;
    bool suppressMoveStopOnNextLeftButtonUp_ = false;
    LayoutEditTraceSession layoutEditTraceSession_{};
};
