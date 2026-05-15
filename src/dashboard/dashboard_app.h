#pragma once

#include <windows.h>

#include <chrono>
#include <memory>
#include <optional>
#include <shellapi.h>
#include <string>
#include <string_view>

#include "config/diagnostics_options.h"
#include "dashboard/dashboard_controller.h"
#include "display/constants.h"
#include "display/monitor.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_trace_session.h"
#include "util/file_path.h"
#include "util/lightweight_mutex.h"
#include "util/trace.h"

class DashboardShellUi;

class DashboardApp : private LayoutEditHost, public DashboardShellHost {
public:
    explicit DashboardApp(const DiagnosticsOptions& diagnosticsOptions = {}, bool bringToFrontOnRun = false);
    ~DashboardApp();
    bool Initialize(HINSTANCE instance);
    const std::string& LastError() const;
    int Run();
    bool InitializeFonts() override;
    void SetRenderConfig(const AppConfig& config);
    void ReleaseFonts() override;
    bool SaveSnapshotPng(const FilePath& imagePath, const SystemSnapshot& snapshot);
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
    void RefreshThemedIcons() override;
    void ApplyThemedIconsToWindow(HWND target) const;
    HICON CreateThemedAppIconForSize(int size) const;
    void InvalidateShell() override;
    void RedrawShellNow() override;
    void EnqueueTelemetryUpdate(const TelemetryUpdate& update) override;
    MonitorPlacementInfo GetWindowPlacementInfo() const override;
    std::optional<FilePath> PromptDiagnosticsSavePath(
        std::string_view defaultFileName, std::string_view filter, std::string_view defaultExtension) const override;
    void ShowError(std::string_view message) const override;

private:
    friend class DashboardShellUi;

    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void BringOnTop();
    bool ApplyConfiguredWallpaper();
    bool ApplyWindowDpi(UINT dpi, const RECT* suggestedRect = nullptr);
    void SetDashboardWindowGeometry(int left, int top, int width, int height, UINT flags, std::string_view reason);
    void RedrawDashboardSurfaceSynchronously();
    void UpdateRendererScale(double scale);
    double ResolveCurrentDisplayScale(UINT dpi) const;
    bool IsLayoutEditMode() const;
    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis) override;
    void StartMoveMode();
    void StartMoveModeAt(POINT cursorAnchorClientPoint);
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
    void RedrawLayoutEditDragFrame();
    void RelayLayoutEditTooltipMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void TraceLayoutEditUiEvent(TracePrefix prefix, const char* event, const std::string& details = {}) const;
    std::string BuildLayoutEditUiTraceState() const;
    bool CreateTrayIcon();
    void RemoveTrayIcon();
    HICON LoadAppIcon(int width, int height);
    void DestroyLoadedIcons(HICON largeIcon, HICON smallIcon) const;
    int WindowWidth() const;
    int WindowHeight() const;
    bool HandleRenderEnvironmentChange(const char* reason);
    void RegisterSessionNotifications();
    void UnregisterSessionNotifications();
    void StartPlacementWatch();
    void StopPlacementWatch();
    void RetryConfigPlacementIfPending();
    bool DrainPendingTelemetryUpdate(TelemetryUpdate& update);
    void StartMoveMode(bool hasCursorAnchorClientPoint, POINT cursorAnchorClientPoint);

    void BeginLayoutEditTraceSession(const char* kind, const std::string& detail) override;
    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override;
    void EndLayoutEditTraceSession(const char* reason) override;

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
    bool ApplyLayoutGuideAdjacentWeights(
        const LayoutEditLayoutTarget& target, size_t separatorIndex, int firstWeight, int secondWeight);
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
    bool bringToFrontOnRun_ = false;
    UINT currentDpi_ = kDefaultDpi;
    LayoutEditController layoutEditController_;
    std::unique_ptr<DashboardShellUi> shellUi_;
    HWND layoutEditTooltipHwnd_ = nullptr;
    std::string layoutEditTooltipText_;
    std::wstring layoutEditTooltipWideText_;
    std::string lastError_;
    bool layoutEditTooltipVisible_ = false;
    bool layoutEditMouseTracking_ = false;
    RECT layoutEditTooltipRect_{};
    bool layoutEditTooltipRectValid_ = false;
    bool layoutEditTooltipRefreshSuppressed_ = false;
    bool sessionNotificationsRegistered_ = false;
    int layoutEditModalUiDepth_ = 0;
    POINT moveCursorAnchorClientPoint_{};
    bool hasMoveCursorAnchorClientPoint_ = false;
    bool suppressMoveStopOnNextLeftButtonUp_ = false;
    LightweightMutex pendingTelemetryLock_;
    TelemetryUpdate pendingTelemetryUpdate_{};
    bool hasPendingTelemetryUpdate_ = false;
    LayoutEditTraceSession layoutEditTraceSession_{};
};
