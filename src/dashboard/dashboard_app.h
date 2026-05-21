#pragma once

#include <windows.h>

#include <chrono>
#include <memory>
#include <optional>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <vector>

#include "config/diagnostics_options.h"
#include "dashboard/dashboard_controller.h"
#include "dashboard/dashboard_titlebar.h"
#include "dashboard/dashboard_tooltip.h"
#include "dashboard/dashboard_window_chrome.h"
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

    enum class NativeTitlebarButton {
        None,
        AppMenu,
        EditLayout,
        Display,
        Close,
    };

    enum class DashboardTooltipOwner {
        None,
        LayoutEdit,
        Titlebar,
    };

    static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TitlebarProbeWndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TitlebarProbeWndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleTitlebarProbeMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();
    void BringOnTop();
    void ScheduleBringToFrontRetries();
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
    bool CreateDashboardTooltip();
    void DestroyDashboardTooltip();
    void HideLayoutEditTooltip(std::string_view reason = "layout_edit_inactive");
    void HideTitlebarTooltip(std::string_view reason = "titlebar_inactive");
    void HideTooltipForLayoutEditUpdate(std::string_view reason);
    void SetLayoutEditTooltipRefreshSuppressed(bool suppressed);
    void UpdateLayoutEditTooltip();
    void RefreshLayoutEditHoverFromCursor();
    bool ShouldIgnoreCoveredLayoutEditPointer(POINT screenPoint, bool allowDuringDrag) const;
    void SuspendCoveredLayoutEditHover();
    void UpdateLayoutEditMouseTracking();
    void RedrawMoveFrame();
    void RedrawLayoutEditDragFrame();
    void RelayLayoutEditTooltipMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void TraceLayoutEditUiEvent(TracePrefix prefix, const char* event, const std::string& details = {}) const;
    void TraceLayoutEditUiEventFmt(TracePrefix prefix, const char* event, const char* format, ...) const;
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
    RECT DashboardClientScreenRect() const;
    DashboardTitlebarFrameMargins ComputeNativeTitlebarFrameMargins(int clientWidth, int clientHeight) const;
    DashboardTitlebarGeometry ResolveNativeTitlebarGeometry(const RECT& dashboardClientRect) const;
    RECT ResolveWindowRectForDashboardClientRect(const RECT& dashboardClientRect) const;
    void UpdateNativeTitlebarHoverFromCursor();
    void UpdateNativeTitlebarProbe();
    bool CreateNativeTitlebarProbe();
    void DestroyNativeTitlebarProbe();
    void ClearNativeTitlebarProbeRegion();
    void UpdateNativeTitlebarProbeRegion(int width, int height);
    void ShowNativeTitlebar(const DashboardTitlebarGeometry& geometry);
    void HideNativeTitlebar();
    bool CreateNativeTitlebarControls();
    void DestroyNativeTitlebarControls();
    void SyncNativeTitlebarControls();
    void UpdateNativeTitlebarControls();
    void ShowNativeTitlebarControls(bool show);
    int NativeTitlebarComboClosedHeight(HWND combo) const;
    int NativeTitlebarComboWindowHeight(HWND combo, const RECT& closedRect) const;
    void PositionNativeTitlebarCombo(HWND combo, const RECT& closedRect);
    RECT NativeTitlebarLayoutComboRect() const;
    RECT NativeTitlebarThemeComboRect() const;
    RECT NativeTitlebarButtonRect(NativeTitlebarButton button) const;
    void PopulateNativeTitlebarCombo(HWND combo,
        const std::vector<std::string>& values,
        std::string_view selected,
        std::vector<std::string>& cache,
        std::string& selectedCache);
    std::vector<std::string> NativeTitlebarLayoutNames() const;
    std::vector<std::string> NativeTitlebarThemeNames() const;
    std::optional<size_t> NativeTitlebarComboSelectionIndex(HWND combo) const;
    NativeTitlebarButton HitTestNativeTitlebarButton(POINT clientPoint) const;
    void PaintNativeTitlebar(HDC hdc) const;
    void PaintNativeTitlebarButton(HDC hdc, NativeTitlebarButton button) const;
    void RefreshNativeTitlebarChrome();
    void InvalidateNativeTitlebar() const;
    void SetNativeTitlebarButtonState(NativeTitlebarButton hovered, NativeTitlebarButton pressed);
    void ResetNativeTitlebarButtonState();
    void UpdateNativeTitlebarButtonHover(POINT screenPoint);
    void UpdateNativeTitlebarTooltip(POINT screenPoint);
    void InvokeNativeTitlebarButton(NativeTitlebarButton button);
    void StartNativeTitlebarHoverTimer();
    void StopNativeTitlebarHoverTimer();
    void StartMoveMode(bool hasCursorAnchorClientPoint,
        POINT cursorAnchorClientPoint,
        bool clampCursorAnchorClientPoint,
        bool placeOnRelease,
        bool keepNativeTitlebarDuringMove);
    void StartMoveModeFromNativeTitlebar(POINT screenPoint);

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
    NOTIFYICONDATAA trayIcon_{};
    MonitorPlacementInfo movePlacementInfo_{};
    HICON appIconLarge_ = nullptr;
    HICON appIconSmall_ = nullptr;
    DiagnosticsOptions diagnosticsOptions_;
    bool bringToFrontOnRun_ = false;
    int bringToFrontRetriesRemaining_ = 0;
    UINT currentDpi_ = kDefaultDpi;
    LayoutEditController layoutEditController_;
    std::unique_ptr<DashboardShellUi> shellUi_;
    DashboardTooltip dashboardTooltip_;
    DashboardTooltipOwner dashboardTooltipOwner_ = DashboardTooltipOwner::None;
    std::string lastError_;
    bool layoutEditMouseTracking_ = false;
    bool layoutEditTooltipRefreshSuppressed_ = false;
    bool sessionNotificationsRegistered_ = false;
    int layoutEditModalUiDepth_ = 0;
    POINT moveCursorAnchorClientPoint_{};
    bool hasMoveCursorAnchorClientPoint_ = false;
    bool clampMoveCursorAnchorClientPoint_ = true;
    bool suppressMoveStopOnNextLeftButtonUp_ = false;
    bool stopMoveModeWhenLeftButtonReleased_ = false;
    bool nativeTitlebarDragMoveActive_ = false;
    HWND titlebarHoverProbeHwnd_ = nullptr;
    HWND titlebarLayoutComboHwnd_ = nullptr;
    HWND titlebarThemeComboHwnd_ = nullptr;
    RECT nativeTitlebarProbeRect_{};
    bool nativeTitlebarVisible_ = false;
    bool nativeTitlebarProbeVisible_ = false;
    bool nativeTitlebarProbeRectValid_ = false;
    bool nativeTitlebarProbeRounded_ = false;
    int nativeTitlebarProbeRegionWidth_ = 0;
    int nativeTitlebarProbeRegionHeight_ = 0;
    bool nativeTitlebarHoverInside_ = false;
    bool nativeTitlebarHoverTimerActive_ = false;
    bool nativeTitlebarControlsVisible_ = false;
    bool nativeTitlebarComboDropdownOpen_ = false;
    DashboardTitlebarTooltipControl nativeTitlebarTooltipControl_ = DashboardTitlebarTooltipControl::None;
    RECT nativeTitlebarTooltipRect_{};
    bool nativeTitlebarTooltipRectValid_ = false;
    BYTE nativeTitlebarProbeAlpha_ = 0;
    NativeTitlebarButton nativeTitlebarHoveredButton_ = NativeTitlebarButton::None;
    NativeTitlebarButton nativeTitlebarPressedButton_ = NativeTitlebarButton::None;
    std::vector<std::string> nativeTitlebarLayoutItems_;
    std::vector<std::string> nativeTitlebarThemeItems_;
    std::string nativeTitlebarSelectedLayout_;
    std::string nativeTitlebarSelectedTheme_;
    DashboardTitlebarPalette nativeTitlebarPalette_{};
    LightweightMutex pendingTelemetryLock_;
    TelemetryUpdate pendingTelemetryUpdate_{};
    bool hasPendingTelemetryUpdate_ = false;
    LayoutEditTraceSession layoutEditTraceSession_{};
};
