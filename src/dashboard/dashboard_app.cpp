#include "dashboard/dashboard_app.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <windowsx.h>
#include <wtsapi32.h>

#include "dashboard/constants.h"
#include "dashboard/dashboard_shell_ui.h"
#include "display/display_config.h"
#include "layout_edit/layout_edit_tooltip_text.h"
#include "resource.h"
#include "util/app_strings.h"
#include "util/lightweight_mutex.h"
#include "util/localization_catalog.h"
#include "util/message_box.h"
#include "util/paths.h"
#include "util/scale.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "widget/app_icon_geometry.h"

namespace {

const UINT kTooltipToolInfoSize = TTTOOLINFOA_V2_SIZE;
constexpr UINT kLayoutEditTooltipFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;

RECT RectFromPoint(RenderPoint point, int radius) {
    return RECT{point.x - radius, point.y - radius, point.x + radius + 1, point.y + radius + 1};
}

constexpr double kScaleEpsilon = 0.0001;
constexpr int kBringToFrontRetryCount = 8;

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

const char* TraceTimingOperationName(LayoutEditHost::TracePhase phase) {
    switch (phase) {
        case LayoutEditHost::TracePhase::Snap:
            return "snap";
        case LayoutEditHost::TracePhase::Apply:
            return "apply";
        case LayoutEditHost::TracePhase::PaintTotal:
            return "paint_total";
        case LayoutEditHost::TracePhase::PaintDraw:
            return "paint_draw";
    }
    return "unknown";
}

POINT ClampPointToWindowBounds(POINT point, int width, int height) {
    const int maxX = std::max(0, width - 1);
    const int maxY = std::max(0, height - 1);
    point.x = std::clamp(point.x, 0L, static_cast<LONG>(maxX));
    point.y = std::clamp(point.y, 0L, static_cast<LONG>(maxY));
    return point;
}

HICON CreateThemedAppIcon(const AppConfig& config, int size) {
    if (!IsValidAppIconSize(size)) {
        return nullptr;
    }
    const AppIconBitmap bitmap = RenderAppIconBitmap(config, size);

    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = bitmap.size;
    header.bV5Height = -bitmap.size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HBITMAP colorBitmap =
        CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (colorBitmap == nullptr || bits == nullptr) {
        return nullptr;
    }
    std::memcpy(bits, bitmap.bgra.data(), bitmap.bgra.size());

    HBITMAP maskBitmap = CreateBitmap(bitmap.size, bitmap.size, 1, 1, nullptr);
    if (maskBitmap == nullptr) {
        DeleteObject(colorBitmap);
        return nullptr;
    }

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = colorBitmap;
    iconInfo.hbmMask = maskBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    return icon;
}

}  // namespace

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions, bool bringToFrontOnRun)
    : renderer_(trace_), diagnosticsOptions_(diagnosticsOptions), layoutEditController_(*this),
      shellUi_(std::make_unique<DashboardShellUi>(*this)), bringToFrontOnRun_(bringToFrontOnRun) {
    renderer_.SetLiveAnimationEnabled(true);
}

DashboardApp::~DashboardApp() = default;

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    controller_.State().config = config;
    renderer_.SetConfig(config);
    rendererDashboardOverlayState_.showLayoutEditGuides =
        controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
    rendererDashboardOverlayState_.similarityIndicatorMode = GetSimilarityIndicatorMode(diagnosticsOptions_);
    SyncDashboardMoveOverlayState();
}

void DashboardApp::UpdateRendererScale(double scale) {
    renderer_.SetRenderScale(scale);
}

UINT DashboardApp::CurrentWindowDpi() const {
    if (hwnd_ != nullptr) {
        return GetDpiForWindow(hwnd_);
    }
    return currentDpi_;
}

double DashboardApp::CurrentRenderScale() const {
    return renderer_.RenderScale();
}

double DashboardApp::ResolveCurrentDisplayScale(UINT dpi) const {
    return ResolveDisplayScale(controller_.State().config, dpi);
}

bool DashboardApp::IsLayoutEditMode() const {
    return controller_.State().isEditingLayout;
}

const AppConfig& DashboardApp::LayoutEditConfig() const {
    return controller_.State().config;
}

DashboardOverlayState& DashboardApp::LayoutDashboardOverlayState() {
    return rendererDashboardOverlayState_;
}

LayoutEditActiveRegions DashboardApp::CollectLayoutEditActiveRegions() const {
    return renderer_.CollectLayoutEditActiveRegions(rendererDashboardOverlayState_);
}

LayoutEditHoverResolution DashboardApp::ResolveLayoutEditHover(RenderPoint clientPoint) const {
    auto timing = trace_.Timings().Measure(trace_, "hover_hit_test");
    return renderer_.ResolveLayoutEditHover(rendererDashboardOverlayState_, clientPoint);
}

double DashboardApp::LayoutEditRenderScale() const {
    return renderer_.RenderScale();
}

int DashboardApp::LayoutEditSimilarityThreshold() const {
    return renderer_.LayoutSimilarityThreshold();
}

void DashboardApp::SetLayoutGuideDragActive(bool active) {
    renderer_.SetLayoutGuideDragActive(active);
}

void DashboardApp::SetLayoutEditInteractiveDragTraceActive(bool active) {
    renderer_.SetInteractiveDragTraceActive(active);
}

void DashboardApp::RebuildLayoutEditArtifacts() {
    renderer_.RebuildEditArtifacts();
}

void DashboardApp::InvalidateLayoutEdit() {
    if (!layoutEditController_.HasActiveDrag()) {
        UpdateLayoutEditTooltip();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int DashboardApp::WindowWidth() const {
    return renderer_.WindowWidth();
}

int DashboardApp::WindowHeight() const {
    return renderer_.WindowHeight();
}

bool DashboardApp::Initialize(HINSTANCE instance) {
    lastError_.clear();
    instance_ = instance;
    InitializeLocalizationCatalog();
    if (!controller_.InitializeSession(*this, diagnosticsOptions_)) {
        lastError_ = controller_.State().lastError;
        return false;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (appIconLarge_ == nullptr) {
        appIconLarge_ = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    }
    if (appIconSmall_ == nullptr) {
        appIconSmall_ = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    }
    wc.hIcon = appIconLarge_;
    wc.hIconSm = appIconSmall_;
    if (!RegisterClassExA(&wc)) {
        return false;
    }

    const AppConfig& config = controller_.State().config;
    RECT placement{100, 100, 100 + WindowWidth(), 100 + WindowHeight()};
    currentDpi_ = GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    if (const auto monitor = FindTargetMonitor(config.display.monitorName); monitor.has_value()) {
        currentDpi_ = monitor->dpi;
        UpdateRendererScale(ResolveCurrentDisplayScale(currentDpi_));
        placement.left = monitor->rect.left + ScaleLogicalToPhysical(config.display.position.x, CurrentRenderScale());
        placement.top = monitor->rect.top + ScaleLogicalToPhysical(config.display.position.y, CurrentRenderScale());
    } else {
        UpdateRendererScale(ResolveCurrentDisplayScale(currentDpi_));
        placement.left = 100 + ScaleLogicalToPhysical(config.display.position.x, CurrentRenderScale());
        placement.top = 100 + ScaleLogicalToPhysical(config.display.position.y, CurrentRenderScale());
    }
    placement.right = placement.left + WindowWidth();
    placement.bottom = placement.top + WindowHeight();

    hwnd_ = CreateWindowExA(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        kAppTitle,
        WS_POPUP,
        placement.left,
        placement.top,
        WindowWidth(),
        WindowHeight(),
        nullptr,
        nullptr,
        instance,
        this);
    if (hwnd_ == nullptr) {
        return false;
    }
    return CreateLayoutEditTooltip();
}

const std::string& DashboardApp::LastError() const {
    return lastError_;
}

void DashboardApp::ApplyConfigPlacement() {
    const AppConfig& config = controller_.State().config;
    UINT targetDpi = hwnd_ != nullptr ? CurrentWindowDpi()
                                      : GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    double targetScale = ResolveCurrentDisplayScale(targetDpi);
    int left = 100 + ScaleLogicalToPhysical(config.display.position.x, targetScale);
    int top = 100 + ScaleLogicalToPhysical(config.display.position.y, targetScale);
    bool monitorResolved = config.display.monitorName.empty();
    if (const auto monitor = FindTargetMonitor(config.display.monitorName); monitor.has_value()) {
        monitorResolved = true;
        targetDpi = monitor->dpi;
        targetScale = ResolveCurrentDisplayScale(targetDpi);
        left = monitor->rect.left + ScaleLogicalToPhysical(config.display.position.x, targetScale);
        top = monitor->rect.top + ScaleLogicalToPhysical(config.display.position.y, targetScale);
    }

    if (!monitorResolved) {
        return;
    }

    const UINT currentDpi = CurrentWindowDpi();
    if (targetDpi != currentDpi) {
        SetWindowPos(hwnd_, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    }

    if ((CurrentWindowDpi() != targetDpi || currentDpi_ != targetDpi ||
         !AreScalesEqual(CurrentRenderScale(), targetScale)) &&
        !ApplyWindowDpi(targetDpi)) {
        return;
    }
    SetDashboardWindowGeometry(
        left, top, WindowWidth(), WindowHeight(), SWP_NOACTIVATE | SWP_NOZORDER, "config_placement");
}

void DashboardApp::SetDashboardWindowGeometry(
    int left, int top, int width, int height, UINT flags, std::string_view reason) {
    if (hwnd_ == nullptr) {
        return;
    }

    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    const bool sizeChanged =
        (windowRect.right - windowRect.left) != width || (windowRect.bottom - windowRect.top) != height;
    if (sizeChanged) {
        renderer_.DiscardWindowRenderTarget(reason);
        // Surface changes render a fresh frame explicitly; do not let USER32 preserve old client pixels.
        flags |= SWP_NOREDRAW | SWP_NOCOPYBITS;
    }

    SetWindowPos(hwnd_, nullptr, left, top, width, height, flags);
    if (sizeChanged) {
        RedrawDashboardSurfaceSynchronously();
    }
}

void DashboardApp::RedrawDashboardSurfaceSynchronously() {
    if (hwnd_ == nullptr) {
        return;
    }

    const auto paintStart = std::chrono::steady_clock::now();
    const SystemSnapshot& snapshot = controller_.State().telemetryUpdate.dump.snapshot;
    const auto drawStart = std::chrono::steady_clock::now();
    SyncDashboardMoveOverlayState();
    if (!renderer_.DrawWindowSynchronously(snapshot, rendererDashboardOverlayState_)) {
        lastError_ = renderer_.LastError();
    }
    const auto drawEnd = std::chrono::steady_clock::now();
    ValidateRect(hwnd_, nullptr);
    KillTimer(hwnd_, kAnimationFrameTimerId);
    const auto paintEnd = std::chrono::steady_clock::now();
    RecordLayoutEditTracePhase(TracePhase::PaintDraw, drawEnd - drawStart);
    RecordLayoutEditTracePhase(TracePhase::PaintTotal, paintEnd - paintStart);
}

bool DashboardApp::HandleRenderEnvironmentChange(const char* reason) {
    renderer_.DiscardWindowRenderTarget(reason != nullptr ? reason : "");
    StartPlacementWatch();
    RetryConfigPlacementIfPending();
    controller_.RefreshTelemetrySelections(*this);
    if (!ApplyWindowDpi(CurrentWindowDpi())) {
        return false;
    }
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void DashboardApp::RegisterSessionNotifications() {
    if (hwnd_ == nullptr || sessionNotificationsRegistered_) {
        return;
    }
    sessionNotificationsRegistered_ = WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION) != FALSE;
}

void DashboardApp::UnregisterSessionNotifications() {
    if (!sessionNotificationsRegistered_ || hwnd_ == nullptr) {
        return;
    }
    WTSUnRegisterSessionNotification(hwnd_);
    sessionNotificationsRegistered_ = false;
}

void DashboardApp::StartPlacementWatch() {
    if (hwnd_ == nullptr || controller_.State().config.display.monitorName.empty()) {
        StopPlacementWatch();
        return;
    }
    SetTimer(hwnd_, kPlacementTimerId, kPlacementTimerMs, nullptr);
    controller_.State().placementWatchActive = true;
}

void DashboardApp::StopPlacementWatch() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kPlacementTimerId);
    }
    controller_.State().placementWatchActive = false;
}

void DashboardApp::RetryConfigPlacementIfPending() {
    if (!controller_.State().placementWatchActive || hwnd_ == nullptr || controller_.State().isMoving) {
        return;
    }
    if (controller_.State().config.display.monitorName.empty() ||
        FindTargetMonitor(controller_.State().config.display.monitorName).has_value()) {
        ApplyConfigPlacement();
        ApplyConfiguredWallpaper();
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
        InvalidateRect(hwnd_, nullptr, FALSE);
        StopPlacementWatch();
    }
}

bool DashboardApp::InitializeFonts() {
    renderer_.SetConfig(controller_.State().config);
    return renderer_.Initialize(hwnd_);
}

void DashboardApp::ReleaseFonts() {
    renderer_.Shutdown();
}

HICON DashboardApp::LoadAppIcon(int width, int height) {
    return static_cast<HICON>(
        LoadImageA(instance_, MAKEINTRESOURCEA(IDI_APP_ICON), IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
}

void DashboardApp::DestroyLoadedIcons(HICON largeIcon, HICON smallIcon) const {
    if (largeIcon != nullptr) {
        DestroyIcon(largeIcon);
    }
    if (smallIcon != nullptr && smallIcon != largeIcon) {
        DestroyIcon(smallIcon);
    }
}

void DashboardApp::ApplyThemedIconsToWindow(HWND target) const {
    if (target == nullptr || !IsWindow(target)) {
        return;
    }
    if (appIconLarge_ != nullptr) {
        SendMessageA(target, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIconLarge_));
    }
    if (appIconSmall_ != nullptr) {
        SendMessageA(target, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIconSmall_));
    }
}

HICON DashboardApp::CreateThemedAppIconForSize(int size) const {
    return CreateThemedAppIcon(controller_.State().config, size);
}

void DashboardApp::RefreshThemedIcons() {
    const AppConfig& config = controller_.State().config;
    HICON largeIcon = CreateThemedAppIcon(config, GetSystemMetrics(SM_CXICON));
    HICON smallIcon = CreateThemedAppIcon(config, GetSystemMetrics(SM_CXSMICON));
    if (largeIcon == nullptr || smallIcon == nullptr) {
        DestroyLoadedIcons(largeIcon, smallIcon);
        return;
    }

    HICON previousLarge = appIconLarge_;
    HICON previousSmall = appIconSmall_;
    appIconLarge_ = largeIcon;
    appIconSmall_ = smallIcon;

    ApplyThemedIconsToWindow(hwnd_);
    if (trayIcon_.cbSize != 0) {
        trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        trayIcon_.hIcon = appIconSmall_;
        Shell_NotifyIconA(NIM_MODIFY, &trayIcon_);
    }
    if (shellUi_ != nullptr) {
        shellUi_->RefreshDialogIcons();
    }
    DestroyLoadedIcons(previousLarge, previousSmall);
}

bool DashboardApp::SaveSnapshotPng(const FilePath& imagePath, const SystemSnapshot& snapshot) {
    renderer_.SetConfig(controller_.State().config);
    rendererDashboardOverlayState_.showLayoutEditGuides =
        controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
    SyncDashboardMoveOverlayState();
    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    return renderer_.SaveSnapshotPng(imagePath, snapshot, rendererDashboardOverlayState_);
}

bool DashboardApp::ApplyWindowDpi(UINT dpi, const RECT* suggestedRect) {
    const UINT targetDpi = std::max(kDefaultDpi, dpi);
    const double targetScale = ResolveCurrentDisplayScale(targetDpi);
    if (currentDpi_ == targetDpi && AreScalesEqual(CurrentRenderScale(), targetScale) && suggestedRect == nullptr) {
        return true;
    }

    currentDpi_ = targetDpi;
    UpdateRendererScale(targetScale);
    // Scale and DPI changes must preserve the presentation timeline; surfaceVersion handles target recreation.
    if (!renderer_.LastError().empty()) {
        lastError_ = renderer_.LastError();
        return false;
    }

    if (suggestedRect != nullptr) {
        const int width = HasExplicitDisplayScale(controller_.State().config.display.scale)
            ? WindowWidth()
            : suggestedRect->right - suggestedRect->left;
        const int height = HasExplicitDisplayScale(controller_.State().config.display.scale)
            ? WindowHeight()
            : suggestedRect->bottom - suggestedRect->top;
        SetDashboardWindowGeometry(
            suggestedRect->left, suggestedRect->top, width, height, SWP_NOZORDER | SWP_NOACTIVATE, "dpi_change");
    }
    return true;
}

bool DashboardApp::WriteDiagnosticsOutputs() {
    return controller_.WriteDiagnosticsOutputs();
}

std::optional<FilePath> DashboardApp::PromptDiagnosticsSavePath(
    std::string_view defaultFileName, std::string_view filter, std::string_view defaultExtension) const {
    return PromptSavePath(hwnd_, GetWorkingDirectory(), defaultFileName, filter, defaultExtension);
}

void DashboardApp::BringOnTop() {
    if (hwnd_ == nullptr) {
        return;
    }

    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    const BOOL foregroundSet = SetForegroundWindow(hwnd_);
    SetActiveWindow(hwnd_);
    trace_.WriteFmt(
        TracePrefix::Diagnostics,
        "bring_to_front hwnd=0x%p visible=%d iconic=%d foreground_set=%d",
        reinterpret_cast<void*>(hwnd_),
        IsWindowVisible(hwnd_) != FALSE,
        IsIconic(hwnd_) != FALSE,
        foregroundSet != FALSE);
}

void DashboardApp::ScheduleBringToFrontRetries() {
    if (hwnd_ == nullptr) {
        return;
    }

    bringToFrontRetriesRemaining_ = kBringToFrontRetryCount;
    SetTimer(hwnd_, kBringToFrontRetryTimerId, kBringToFrontRetryTimerMs, nullptr);
}

bool DashboardApp::ApplyConfiguredWallpaper() {
    return ::ApplyConfiguredWallpaper(controller_.State().config, trace_);
}

bool DashboardApp::ApplyLayoutGuideWeights(const LayoutEditLayoutTarget& target, const std::vector<int>& weights) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyLayoutGuideWeights(*this, target, weights);
    RecordLayoutEditTracePhase(TracePhase::Apply, std::chrono::steady_clock::now() - start);
    return applied;
}

bool DashboardApp::ApplyLayoutGuideAdjacentWeights(
    const LayoutEditLayoutTarget& target, size_t separatorIndex, int firstWeight, int secondWeight) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied =
        controller_.ApplyLayoutGuideAdjacentWeights(*this, target, separatorIndex, firstWeight, secondWeight);
    RecordLayoutEditTracePhase(TracePhase::Apply, std::chrono::steady_clock::now() - start);
    return applied;
}

bool DashboardApp::ApplyMetricListOrder(
    const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyMetricListOrder(*this, widget, metricRefs);
    RecordLayoutEditTracePhase(TracePhase::Apply, std::chrono::steady_clock::now() - start);
    return applied;
}

bool DashboardApp::ApplyContainerChildOrder(const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyContainerChildOrder(*this, key, fromIndex, toIndex);
    RecordLayoutEditTracePhase(TracePhase::Apply, std::chrono::steady_clock::now() - start);
    return applied;
}

bool DashboardApp::ApplyLayoutEditValue(LayoutEditParameter parameter, double value) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyLayoutEditValue(*this, parameter, value);
    RecordLayoutEditTracePhase(TracePhase::Apply, std::chrono::steady_clock::now() - start);
    return applied;
}

std::optional<int> DashboardApp::EvaluateLayoutWidgetExtentForWeights(
    const LayoutEditLayoutTarget& target,
    const std::vector<int>& weights,
    const LayoutEditWidgetIdentity& widget,
    LayoutGuideAxis axis) {
    return controller_.EvaluateLayoutWidgetExtentForWeights(*this, target, weights, widget, axis);
}

bool DashboardApp::CreateTrayIcon() {
    trayIcon_ = {};
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kTrayMessage;
    trayIcon_.hIcon = appIconSmall_ != nullptr ? appIconSmall_ : LoadIconA(nullptr, IDI_APPLICATION);
    strcpy_s(trayIcon_.szTip, kAppTitle);
    return Shell_NotifyIconA(NIM_ADD, &trayIcon_) == TRUE;
}

void DashboardApp::RemoveTrayIcon() {
    if (trayIcon_.cbSize != 0) {
        Shell_NotifyIconA(NIM_DELETE, &trayIcon_);
    }
}

void DashboardApp::StartMoveMode() {
    StartMoveMode(false, POINT{});
}

void DashboardApp::StartMoveModeAt(POINT cursorAnchorClientPoint) {
    StartMoveMode(true, cursorAnchorClientPoint);
}

void DashboardApp::StartMoveMode(bool hasCursorAnchorClientPoint, POINT cursorAnchorClientPoint) {
    if (controller_.State().isEditingLayout) {
        layoutEditController_.CancelInteraction();
    }
    HideLayoutEditTooltip();
    moveCursorAnchorClientPoint_ = cursorAnchorClientPoint;
    hasMoveCursorAnchorClientPoint_ = hasCursorAnchorClientPoint;
    suppressMoveStopOnNextLeftButtonUp_ = false;
    controller_.State().isMoving = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    SyncDashboardMoveOverlayState();
    RedrawMoveFrame();
}

void DashboardApp::StopMoveMode() {
    if (!controller_.State().isMoving) {
        return;
    }
    hasMoveCursorAnchorClientPoint_ = false;
    suppressMoveStopOnNextLeftButtonUp_ = false;
    controller_.State().isMoving = false;
    KillTimer(hwnd_, kMoveTimerId);
    HideLayoutEditTooltip();
    SyncDashboardMoveOverlayState();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::UpdateMoveTracking() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }

    POINT cursorClientOffset{};
    if (hasMoveCursorAnchorClientPoint_) {
        cursorClientOffset = ClampPointToWindowBounds(moveCursorAnchorClientPoint_, WindowWidth(), WindowHeight());
        moveCursorAnchorClientPoint_ = cursorClientOffset;
    } else {
        int cursorOffset = ScaleLogicalToPhysical(24, CurrentWindowDpi());
        cursorOffset = std::max(
            cursorOffset, renderer_.Renderer().TextMetrics().smallText + ScaleLogicalToPhysical(8, CurrentWindowDpi()));

        cursorClientOffset.x = WindowWidth() / 2;
        cursorClientOffset.y = cursorOffset;
        cursorClientOffset = ClampPointToWindowBounds(cursorClientOffset, WindowWidth(), WindowHeight());
    }
    const int x = cursor.x - cursorClientOffset.x;
    const int y = cursor.y - cursorClientOffset.y;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
    SyncDashboardMoveOverlayState();
}

void DashboardApp::SyncDashboardMoveOverlayState() {
    auto& overlayState = rendererDashboardOverlayState_.moveOverlay;
    if (!controller_.State().isMoving) {
        overlayState = {};
        return;
    }

    overlayState.visible = true;
    overlayState.monitorName = movePlacementInfo_.monitorName;
    overlayState.relativePosition = RenderPoint{
        static_cast<int>(movePlacementInfo_.relativePosition.x),
        static_cast<int>(movePlacementInfo_.relativePosition.y)};
    overlayState.monitorScale = ScaleFromDpi(movePlacementInfo_.dpi);
}

HWND DashboardApp::WindowHandle() const {
    return hwnd_;
}

Trace& DashboardApp::TraceLog() {
    return trace_;
}

DashboardRenderer& DashboardApp::Renderer() {
    return renderer_;
}

const DashboardRenderer& DashboardApp::Renderer() const {
    return renderer_;
}

DashboardOverlayState& DashboardApp::RendererDashboardOverlayState() {
    return rendererDashboardOverlayState_;
}

const DashboardOverlayState& DashboardApp::RendererDashboardOverlayState() const {
    return rendererDashboardOverlayState_;
}

void DashboardApp::InvalidateShell() {
    if (!layoutEditController_.HasActiveDrag()) {
        UpdateLayoutEditTooltip();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::RedrawShellNow() {
    UpdateLayoutEditTooltip();
    if (hwnd_ == nullptr) {
        return;
    }
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void DashboardApp::EnqueueTelemetryUpdate(const TelemetryUpdate& update) {
    {
        const LightweightMutexLock lock(pendingTelemetryLock_);
        pendingTelemetryUpdate_ = update;
        hasPendingTelemetryUpdate_ = true;
    }
    if (hwnd_ != nullptr) {
        PostMessageA(hwnd_, kTelemetryUpdateMessage, 0, 0);
    }
}

bool DashboardApp::DrainPendingTelemetryUpdate(TelemetryUpdate& update) {
    const LightweightMutexLock lock(pendingTelemetryLock_);
    if (!hasPendingTelemetryUpdate_) {
        return false;
    }
    update = std::move(pendingTelemetryUpdate_);
    pendingTelemetryUpdate_ = {};
    hasPendingTelemetryUpdate_ = false;
    return true;
}

MonitorPlacementInfo DashboardApp::GetWindowPlacementInfo() const {
    return hwnd_ != nullptr ? GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale)
                            : movePlacementInfo_;
}

void DashboardApp::ShowError(std::string_view message) const {
    ShowAppMessageBox(hwnd_, message, MB_ICONERROR);
}

bool DashboardApp::CreateLayoutEditTooltip() {
    layoutEditTooltipHwnd_ = CreateWindowExA(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSA,
        nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        hwnd_,
        nullptr,
        instance_,
        nullptr);
    if (layoutEditTooltipHwnd_ == nullptr) {
        return false;
    }

    SetWindowPos(
        layoutEditTooltipHwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);

    TOOLINFOA toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.hwnd = hwnd_;
    toolInfo.uId = 1;
    toolInfo.rect = clientRect;
    layoutEditTooltipText_.clear();
    toolInfo.lpszText = layoutEditTooltipText_.data();
    const LRESULT addToolResult =
        SendMessageA(layoutEditTooltipHwnd_, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&toolInfo));
    const LRESULT activateResult = SendMessageA(layoutEditTooltipHwnd_, TTM_ACTIVATE, TRUE, 0);
    SendMessageA(layoutEditTooltipHwnd_, TTM_SETDELAYTIME, TTDT_INITIAL, 0);
    SendMessageA(layoutEditTooltipHwnd_, TTM_SETDELAYTIME, TTDT_RESHOW, 0);
    SendMessageA(layoutEditTooltipHwnd_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    SendMessageA(layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
    (void)addToolResult;
    (void)activateResult;
    return true;
}

void DashboardApp::DestroyLayoutEditTooltip() {
    if (layoutEditTooltipHwnd_ != nullptr) {
        DestroyWindow(layoutEditTooltipHwnd_);
        layoutEditTooltipHwnd_ = nullptr;
    }
    layoutEditTooltipText_.clear();
    layoutEditTooltipVisible_ = false;
}

void DashboardApp::TraceLayoutEditUiEvent(TracePrefix prefix, const char* event, const std::string& details) const {
    const auto& state = controller_.State();
    if (state.diagnostics == nullptr) {
        return;
    }

    std::string text = event;
    if (!details.empty()) {
        AppendFormat(text, " %s", details.c_str());
    }
    const std::string uiState = BuildLayoutEditUiTraceState();
    if (!uiState.empty()) {
        AppendFormat(text, " %s", uiState.c_str());
    }
    state.diagnostics->WriteTraceMarker(prefix, text);
}

void DashboardApp::TraceLayoutEditUiEventFmt(TracePrefix prefix, const char* event, const char* format, ...) const {
    va_list args;
    va_start(args, format);
    const std::string text = FormatTextV(format, args);
    va_end(args);
    TraceLayoutEditUiEvent(prefix, event, text);
}

std::string DashboardApp::BuildLayoutEditUiTraceState() const {
    const auto& state = controller_.State();
    const HWND capture = GetCapture();
    const char* captureText = capture == nullptr ? "none" : (capture == hwnd_ ? "dashboard" : "other");
    std::string trace = FormatText(
        "layout=\"%s\" editing=%s moving=%s modal_depth=%d tooltip_visible=%s tooltip_suppressed=%s "
        "tooltip_rect_valid=%s mouse_tracking=%s drag_active=%s capture=\"%s\"",
        state.config.display.layout.c_str(),
        Trace::BoolText(state.isEditingLayout),
        Trace::BoolText(state.isMoving),
        layoutEditModalUiDepth_,
        Trace::BoolText(layoutEditTooltipVisible_),
        Trace::BoolText(layoutEditTooltipRefreshSuppressed_),
        Trace::BoolText(layoutEditTooltipRectValid_),
        Trace::BoolText(layoutEditMouseTracking_),
        Trace::BoolText(layoutEditController_.HasActiveDrag()),
        captureText);

    LayoutEditController::TooltipTarget target;
    if (const_cast<LayoutEditController&>(layoutEditController_).CurrentTooltipTarget(target)) {
        AppendFormat(
            trace,
            " target=\"%s\" target_point=\"%d,%d\"",
            LayoutEditTooltipPayloadTraceKind(target.payload),
            target.clientPoint.x,
            target.clientPoint.y);
    } else {
        AppendFormat(trace, " target=\"none\"");
    }

    POINT cursor{};
    if (GetCursorPos(&cursor) != FALSE) {
        AppendFormat(trace, " cursor_screen=\"%d,%d\"", cursor.x, cursor.y);
        if (hwnd_ != nullptr) {
            POINT clientPoint = cursor;
            ScreenToClient(hwnd_, &clientPoint);
            AppendFormat(trace, " cursor_client=\"%d,%d\"", clientPoint.x, clientPoint.y);
        }
    }
    return trace;
}

void DashboardApp::HideLayoutEditTooltip() {
    if (layoutEditTooltipHwnd_ == nullptr || !layoutEditTooltipVisible_) {
        return;
    }

    TOOLINFOA toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = hwnd_;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.uId = 1;
    SendMessageA(layoutEditTooltipHwnd_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&toolInfo));
    TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "hide");
    layoutEditTooltipVisible_ = false;
    layoutEditTooltipRectValid_ = false;
}

void DashboardApp::SetLayoutEditTooltipRefreshSuppressed(bool suppressed) {
    if (layoutEditTooltipRefreshSuppressed_ == suppressed) {
        return;
    }
    layoutEditTooltipRefreshSuppressed_ = suppressed;
    TraceLayoutEditUiEvent(
        TracePrefix::LayoutEditTooltip, "suppression", suppressed ? "value=\"true\"" : "value=\"false\"");
    if (suppressed) {
        HideLayoutEditTooltip();
    }
}

bool DashboardApp::ShouldIgnoreCoveredLayoutEditPointer(POINT screenPoint, bool allowDuringDrag) const {
    if (shellUi_ == nullptr || !controller_.State().isEditingLayout || controller_.State().isMoving ||
        shellUi_->IsLayoutEditModalUiActive()) {
        return false;
    }
    if (allowDuringDrag && layoutEditController_.HasActiveDrag()) {
        return false;
    }
    return shellUi_->ShouldDashboardIgnoreMouse(screenPoint);
}

void DashboardApp::SuspendCoveredLayoutEditHover() {
    if (!controller_.State().isEditingLayout || controller_.State().isMoving || shellUi_ == nullptr ||
        shellUi_->IsLayoutEditModalUiActive() || layoutEditController_.HasActiveDrag()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "suspend", "reason=\"inactive_or_drag\"");
        HideLayoutEditTooltip();
        return;
    }

    TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "suspend", "reason=\"covered_pointer\"");
    layoutEditController_.HandleMouseLeave();
    HideLayoutEditTooltip();
}

void DashboardApp::UpdateLayoutEditTooltip() {
    if (layoutEditTooltipRefreshSuppressed_) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"suppressed\"");
        HideLayoutEditTooltip();
        return;
    }
    if (layoutEditTooltipHwnd_ == nullptr || !controller_.State().isEditingLayout || controller_.State().isMoving ||
        shellUi_->IsLayoutEditModalUiActive()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"inactive\"");
        HideLayoutEditTooltip();
        return;
    }
    if (layoutEditController_.HasActiveDrag()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"active_drag\"");
        HideLayoutEditTooltip();
        return;
    }

    POINT screenPoint{};
    if (GetCursorPos(&screenPoint) && ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"covered_pointer\"");
        SuspendCoveredLayoutEditHover();
        return;
    }

    const bool wasVisible = layoutEditTooltipVisible_;
    const std::string previousText = layoutEditTooltipText_;
    const RECT previousRect = layoutEditTooltipRect_;
    const bool previousRectValid = layoutEditTooltipRectValid_;
    LayoutEditController::TooltipTarget target;
    if (!layoutEditController_.CurrentTooltipTarget(target)) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"no_target\"");
        HideLayoutEditTooltip();
        return;
    }

    const RenderPoint clientPoint = target.clientPoint;
    std::string tooltipError;
    std::string tooltipText;
    if (!BuildLayoutEditTooltipTextForPayload(controller_.State().config, target.payload, tooltipText, &tooltipError)) {
        const char* reasonText = tooltipError.empty() ? "unsupported_target" : tooltipError.c_str();
        TraceLayoutEditUiEventFmt(TracePrefix::LayoutEditTooltip, "update_abort", "reason=\"%s\"", reasonText);
        HideLayoutEditTooltip();
        return;
    }
    layoutEditTooltipText_ = std::move(tooltipText);

    const int tooltipOffsetX = ScaleLogicalToPhysical(28, CurrentWindowDpi());
    const int tooltipOffsetY = ScaleLogicalToPhysical(24, CurrentWindowDpi());
    const int tooltipRadius = ScaleLogicalToPhysical(10, CurrentWindowDpi());
    layoutEditTooltipRect_ = RectFromPoint(clientPoint, tooltipRadius);
    layoutEditTooltipRectValid_ = true;

    TOOLINFOA toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = hwnd_;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.uId = 1;
    toolInfo.rect = layoutEditTooltipRect_;
    toolInfo.lpszText = layoutEditTooltipText_.data();
    SendMessageA(layoutEditTooltipHwnd_, TTM_UPDATETIPTEXTA, 0, reinterpret_cast<LPARAM>(&toolInfo));
    SendMessageA(layoutEditTooltipHwnd_, TTM_NEWTOOLRECTA, 0, reinterpret_cast<LPARAM>(&toolInfo));
    SendMessageA(layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
    POINT tooltipScreenPoint{clientPoint.x + tooltipOffsetX, clientPoint.y + tooltipOffsetY};
    ClientToScreen(hwnd_, &tooltipScreenPoint);
    SendMessageA(layoutEditTooltipHwnd_, TTM_TRACKPOSITION, 0, MAKELPARAM(tooltipScreenPoint.x, tooltipScreenPoint.y));
    SendMessageA(layoutEditTooltipHwnd_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&toolInfo));

    MSG msg{};
    msg.hwnd = hwnd_;
    msg.message = WM_MOUSEMOVE;
    msg.wParam = 0;
    msg.lParam = MAKELPARAM(clientPoint.x, clientPoint.y);
    SendMessageA(layoutEditTooltipHwnd_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
    SendMessageA(layoutEditTooltipHwnd_, TTM_UPDATE, 0, 0);
    ShowWindow(layoutEditTooltipHwnd_, SW_SHOWNOACTIVATE);
    layoutEditTooltipVisible_ = true;

    if (!wasVisible || previousText != layoutEditTooltipText_ || previousRectValid != layoutEditTooltipRectValid_ ||
        !RectsEqual(previousRect, layoutEditTooltipRect_)) {
        TraceLayoutEditUiEventFmt(
            TracePrefix::LayoutEditTooltip,
            "show",
            "payload=\"%s\" client_point=\"%d,%d\" text=\"%s\"",
            LayoutEditTooltipPayloadTraceKind(target.payload),
            clientPoint.x,
            clientPoint.y,
            layoutEditTooltipText_.c_str());
    }
}

void DashboardApp::RefreshLayoutEditHoverFromCursor() {
    TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "refresh_begin");
    if (layoutEditTooltipRefreshSuppressed_) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "refresh_skip", "reason=\"suppressed\"");
        HideLayoutEditTooltip();
        return;
    }
    if (!controller_.State().isEditingLayout || controller_.State().isMoving || shellUi_ == nullptr ||
        shellUi_->IsLayoutEditModalUiActive()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "refresh_skip", "reason=\"inactive\"");
        HideLayoutEditTooltip();
        return;
    }
    if (layoutEditController_.HasActiveDrag()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "refresh_skip", "reason=\"active_drag\"");
        return;
    }

    POINT screenPoint{};
    if (!GetCursorPos(&screenPoint) || ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "refresh_skip", "reason=\"covered_or_no_cursor\"");
        SuspendCoveredLayoutEditHover();
        return;
    }

    POINT clientPointWin32 = screenPoint;
    ScreenToClient(hwnd_, &clientPointWin32);
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    if (!PtInRect(&clientRect, clientPointWin32)) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditHover, "refresh_skip", "reason=\"cursor_outside_client\"");
        layoutEditController_.HandleMouseLeave();
        HideLayoutEditTooltip();
        return;
    }

    UpdateLayoutEditMouseTracking();
    layoutEditController_.HandleMouseMove(RenderPoint{clientPointWin32.x, clientPointWin32.y});
    TraceLayoutEditUiEventFmt(
        TracePrefix::LayoutEditHover, "refresh_move", "client_point=\"%d,%d\"", clientPointWin32.x, clientPointWin32.y);
    UpdateLayoutEditTooltip();
}

void DashboardApp::UpdateLayoutEditMouseTracking() {
    if (hwnd_ == nullptr) {
        return;
    }
    if (!controller_.State().isEditingLayout) {
        layoutEditMouseTracking_ = false;
        return;
    }
    if (layoutEditMouseTracking_) {
        return;
    }

    TRACKMOUSEEVENT trackMouseEvent{};
    trackMouseEvent.cbSize = sizeof(trackMouseEvent);
    trackMouseEvent.dwFlags = TME_LEAVE;
    trackMouseEvent.hwndTrack = hwnd_;
    if (TrackMouseEvent(&trackMouseEvent) != FALSE) {
        layoutEditMouseTracking_ = true;
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditMouseTracking, "start");
    } else {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditMouseTracking, "start_failed");
    }
}

void DashboardApp::RedrawMoveFrame() {
    if (hwnd_ == nullptr || !controller_.State().isMoving) {
        return;
    }

    // WM_TIMER and WM_PAINT are low-priority; move feedback must present from pointer traffic.
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void DashboardApp::RedrawLayoutEditDragFrame() {
    if (hwnd_ == nullptr || !layoutEditController_.HasActiveDrag()) {
        return;
    }

    // Perf: WM_PAINT is low-priority and mouse-move storms can starve it; drag feedback must present per input.
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void DashboardApp::RelayLayoutEditTooltipMouseMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (layoutEditTooltipHwnd_ == nullptr || !layoutEditTooltipRectValid_) {
        return;
    }

    MSG msg{};
    msg.hwnd = hwnd_;
    msg.message = message;
    msg.wParam = wParam;
    msg.lParam = lParam;
    SendMessageA(layoutEditTooltipHwnd_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
}

int DashboardApp::Run() {
    if (bringToFrontOnRun_) {
        BringOnTop();
        ScheduleBringToFrontRetries();
    } else {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (shellUi_ != nullptr && shellUi_->HandleDialogMessage(&msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK DashboardApp::WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* app = static_cast<DashboardApp*>(create->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DashboardApp::WndProcThunk));
        app->hwnd_ = hwnd;
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK DashboardApp::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    return app != nullptr ? app->HandleMessage(message, wParam, lParam) : DefWindowProcA(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    DashboardSessionState& state = controller_.State();
    switch (message) {
        case WM_CREATE:
            currentDpi_ = CurrentWindowDpi();
            UpdateRendererScale(ResolveCurrentDisplayScale(currentDpi_));
            if (!InitializeFonts()) {
                return -1;
            }
            RegisterSessionNotifications();
            StartPlacementWatch();
            ApplyConfigPlacement();
            RefreshThemedIcons();
            CreateTrayIcon();
            return 0;
        case WM_TIMER:
            if (wParam == kMoveTimerId) {
                UpdateMoveTracking();
                RedrawMoveFrame();
                return 0;
            }
            if (wParam == kAnimationFrameTimerId) {
                KillTimer(hwnd_, kAnimationFrameTimerId);
                return 0;
            }
            if (wParam == kPlacementTimerId) {
                RetryConfigPlacementIfPending();
                return 0;
            }
            if (wParam == kBringToFrontRetryTimerId) {
                if (bringToFrontRetriesRemaining_ <= 0) {
                    KillTimer(hwnd_, kBringToFrontRetryTimerId);
                    return 0;
                }

                --bringToFrontRetriesRemaining_;
                BringOnTop();
                if (bringToFrontRetriesRemaining_ <= 0 || GetForegroundWindow() == hwnd_) {
                    bringToFrontRetriesRemaining_ = 0;
                    KillTimer(hwnd_, kBringToFrontRetryTimerId);
                }
                return 0;
            }
            return 0;
        case kTelemetryUpdateMessage: {
            TelemetryUpdate update;
            while (DrainPendingTelemetryUpdate(update)) {
                if (!controller_.HandleTelemetryUpdate(*this, update)) {
                    DestroyWindow(hwnd_);
                    return 0;
                }
            }
            return 0;
        }
        case WM_ACTIVATE:
            if (shellUi_ != nullptr) {
                TraceLayoutEditUiEventFmt(
                    TracePrefix::LayoutEditUi, "wm_activate", "active_state=\"%d\"", static_cast<int>(LOWORD(wParam)));
                RefreshLayoutEditHoverFromCursor();
            }
            break;
        case WM_MOUSEACTIVATE:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT screenPoint{};
                if (GetCursorPos(&screenPoint) &&
                    (shellUi_ == nullptr || !shellUi_->ShouldDashboardIgnoreMouse(screenPoint))) {
                    BringOnTop();
                    return MA_ACTIVATE;
                }
            }
            break;
        case WM_NCHITTEST: {
            const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (shellUi_ != nullptr && shellUi_->ShouldDashboardIgnoreMouse(screenPoint)) {
                return HTTRANSPARENT;
            }
            break;
        }
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            LayoutEditController::TooltipTarget layoutEditTarget;
            const LayoutEditController::TooltipTarget* layoutEditTargetPtr = nullptr;
            if (point.x == -1 && point.y == -1) {
                RECT rect{};
                GetWindowRect(hwnd_, &rect);
                point.x = rect.left + 24;
                point.y = rect.top + 24;
            } else if (state.isEditingLayout && !state.isMoving) {
                if (ShouldIgnoreCoveredLayoutEditPointer(point, true)) {
                    SuspendCoveredLayoutEditHover();
                    return 0;
                }
                POINT clientPoint = point;
                ScreenToClient(hwnd_, &clientPoint);
                RECT clientRect{};
                GetClientRect(hwnd_, &clientRect);
                if (PtInRect(&clientRect, clientPoint)) {
                    layoutEditController_.HandleMouseMove(RenderPoint{clientPoint.x, clientPoint.y});
                    layoutEditTargetPtr =
                        layoutEditController_.CurrentTooltipTarget(layoutEditTarget) ? &layoutEditTarget : nullptr;
                }
            }
            if (state.isMoving) {
                StopMoveMode();
            }
            shellUi_->ShowContextMenu(DashboardShellUi::MenuSource::AppWindow, point, layoutEditTargetPtr);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (state.isEditingLayout && !state.isMoving && !shellUi_->IsLayoutEditModalUiActive()) {
                RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                POINT screenPoint{clientPoint.x, clientPoint.y};
                ClientToScreen(hwnd_, &screenPoint);
                if (ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                    SuspendCoveredLayoutEditHover();
                    return 0;
                }
                if (layoutEditController_.HandleLButtonDown(hwnd_, clientPoint)) {
                    LayoutEditController::TooltipTarget target;
                    shellUi_->SyncLayoutEditDialogSelection(
                        layoutEditController_.CurrentTooltipTarget(target) ? &target : nullptr, false);
                    UpdateLayoutEditTooltip();
                    RedrawLayoutEditDragFrame();
                    return 0;
                }
            }
            break;
        case WM_LBUTTONDBLCLK: {
            if (shellUi_->IsLayoutEditModalUiActive()) {
                return 0;
            }
            LayoutEditController::TooltipTarget layoutEditTarget;
            const LayoutEditController::TooltipTarget* layoutEditTargetPtr = nullptr;
            if (state.isEditingLayout && !state.isMoving) {
                const RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                POINT screenPoint{clientPoint.x, clientPoint.y};
                ClientToScreen(hwnd_, &screenPoint);
                if (ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                    SuspendCoveredLayoutEditHover();
                    return 0;
                }
                layoutEditController_.HandleMouseMove(clientPoint);
                layoutEditTargetPtr =
                    layoutEditController_.CurrentTooltipTarget(layoutEditTarget) ? &layoutEditTarget : nullptr;
            }
            const POINT cursorAnchorPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            shellUi_->InvokeDefaultAction(
                DashboardShellUi::MenuSource::AppWindow, layoutEditTargetPtr, &cursorAnchorPoint);
            if (controller_.State().isMoving) {
                suppressMoveStopOnNextLeftButtonUp_ = true;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (state.isMoving) {
                UpdateMoveTracking();
                RedrawMoveFrame();
                return 0;
            }
            if (state.isEditingLayout && !state.isMoving && !shellUi_->IsLayoutEditModalUiActive()) {
                UpdateLayoutEditMouseTracking();
                RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                POINT screenPoint{clientPoint.x, clientPoint.y};
                ClientToScreen(hwnd_, &screenPoint);
                if (ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                    SuspendCoveredLayoutEditHover();
                    return 0;
                }
                layoutEditController_.HandleMouseMove(clientPoint);
                if (layoutEditController_.HasActiveDrag()) {
                    RedrawLayoutEditDragFrame();
                } else {
                    UpdateLayoutEditTooltip();
                }
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            layoutEditMouseTracking_ = false;
            if (state.isEditingLayout && !state.isMoving && !shellUi_->IsLayoutEditModalUiActive()) {
                TraceLayoutEditUiEvent(TracePrefix::LayoutEditUi, "wm_mouseleave");
                POINT screenPoint{};
                if (GetCursorPos(&screenPoint)) {
                    if (ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                        SuspendCoveredLayoutEditHover();
                        return 0;
                    }
                    POINT clientPointWin32 = screenPoint;
                    ScreenToClient(hwnd_, &clientPointWin32);
                    RECT clientRect{};
                    GetClientRect(hwnd_, &clientRect);
                    if (PtInRect(&clientRect, clientPointWin32)) {
                        TraceLayoutEditUiEvent(
                            TracePrefix::LayoutEditUi, "wm_mouseleave_ignore", "reason=\"cursor_still_in_client\"");
                        return 0;
                    }
                }
                layoutEditController_.HandleMouseLeave();
                HideLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (state.isEditingLayout && !state.isMoving && !shellUi_->IsLayoutEditModalUiActive()) {
                RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                POINT screenPoint{clientPoint.x, clientPoint.y};
                ClientToScreen(hwnd_, &screenPoint);
                if (ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                    SuspendCoveredLayoutEditHover();
                    return 0;
                }
                if (layoutEditController_.HandleLButtonUp(clientPoint)) {
                    LayoutEditController::TooltipTarget target;
                    shellUi_->SyncLayoutEditDialogSelection(
                        layoutEditController_.CurrentTooltipTarget(target) ? &target : nullptr, false);
                    UpdateLayoutEditTooltip();
                    return 0;
                }
            }
            if (state.isMoving) {
                if (suppressMoveStopOnNextLeftButtonUp_) {
                    suppressMoveStopOnNextLeftButtonUp_ = false;
                    return 0;
                }
                StopMoveMode();
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (state.isMoving) {
                    StopMoveMode();
                    return 0;
                }
                if (state.isEditingLayout) {
                    layoutEditController_.CancelInteraction();
                    HideLayoutEditTooltip();
                    return 0;
                }
            }
            break;
        case WM_CAPTURECHANGED:
            if (state.isEditingLayout && !state.isMoving && !shellUi_->IsLayoutEditModalUiActive()) {
                const bool handled = layoutEditController_.HandleCaptureChanged(hwnd_, reinterpret_cast<HWND>(lParam));
                TraceLayoutEditUiEventFmt(
                    TracePrefix::LayoutEditUi,
                    "wm_capturechanged",
                    "new_owner=\"%s\" handled=\"%s\"",
                    reinterpret_cast<HWND>(lParam) == nullptr
                        ? "none"
                        : (reinterpret_cast<HWND>(lParam) == hwnd_ ? "dashboard" : "other"),
                    handled ? "true" : "false");
                if (handled) {
                    UpdateLayoutEditTooltip();
                    return 0;
                }
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && state.isEditingLayout && !state.isMoving &&
                !shellUi_->IsLayoutEditModalUiActive()) {
                POINT screenPoint{};
                if (GetCursorPos(&screenPoint) && ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                    SuspendCoveredLayoutEditHover();
                    return TRUE;
                }
                layoutEditController_.HandleSetCursor(hwnd_);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            shellUi_->HandleExitRequest();
            return 0;
        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                POINT point{};
                GetCursorPos(&point);
                shellUi_->ShowContextMenu(DashboardShellUi::MenuSource::TrayIcon, point, nullptr);
                return 0;
            }
            if (lParam == WM_LBUTTONDBLCLK) {
                shellUi_->InvokeDefaultAction(DashboardShellUi::MenuSource::TrayIcon, nullptr);
                return 0;
            }
            break;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_DPICHANGED:
            if (!ApplyWindowDpi(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam))) {
                return -1;
            }
            if (layoutEditTooltipHwnd_ != nullptr) {
                SendMessageA(
                    layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
            }
            UpdateLayoutEditTooltip();
            if (state.isMoving) {
                UpdateMoveTracking();
            } else {
                movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_DISPLAYCHANGE:
            if (!HandleRenderEnvironmentChange("display_change")) {
                return -1;
            }
            return 0;
        case WM_DEVICECHANGE:
        case WM_SETTINGCHANGE:
            if (!HandleRenderEnvironmentChange(message == WM_DEVICECHANGE ? "device_change" : "setting_change")) {
                return -1;
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            switch (wParam) {
                case WTS_CONSOLE_CONNECT:
                case WTS_CONSOLE_DISCONNECT:
                case WTS_REMOTE_CONNECT:
                case WTS_REMOTE_DISCONNECT:
                case WTS_SESSION_LOGON:
                case WTS_SESSION_LOGOFF:
                case WTS_SESSION_LOCK:
                case WTS_SESSION_UNLOCK:
                    if (!HandleRenderEnvironmentChange("session_change")) {
                        return -1;
                    }
                    return 0;
                default:
                    return 0;
            }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            KillTimer(hwnd_, kMoveTimerId);
            KillTimer(hwnd_, kPlacementTimerId);
            KillTimer(hwnd_, kAnimationFrameTimerId);
            KillTimer(hwnd_, kBringToFrontRetryTimerId);
            if (state.telemetry != nullptr) {
                state.telemetry->Shutdown();
            }
            UnregisterSessionNotifications();
            DestroyLayoutEditTooltip();
            if (state.diagnostics != nullptr) {
                state.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("ui_done"));
            }
            RemoveTrayIcon();
            ReleaseFonts();
            {
                HICON largeIcon = appIconLarge_;
                HICON smallIcon = appIconSmall_;
                appIconLarge_ = nullptr;
                appIconSmall_ = nullptr;
                DestroyLoadedIcons(largeIcon, smallIcon);
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(hwnd_, message, wParam, lParam);
    }

    return DefWindowProcA(hwnd_, message, wParam, lParam);
}

void DashboardApp::Paint() {
    const auto paintStart = std::chrono::steady_clock::now();
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    const SystemSnapshot& snapshot = controller_.State().telemetryUpdate.dump.snapshot;
    const auto drawStart = std::chrono::steady_clock::now();
    SyncDashboardMoveOverlayState();
    renderer_.DrawWindow(snapshot, rendererDashboardOverlayState_);
    const auto drawEnd = std::chrono::steady_clock::now();
    EndPaint(hwnd_, &ps);
    KillTimer(hwnd_, kAnimationFrameTimerId);
    const auto paintEnd = std::chrono::steady_clock::now();
    RecordLayoutEditTracePhase(TracePhase::PaintDraw, drawEnd - drawStart);
    RecordLayoutEditTracePhase(TracePhase::PaintTotal, paintEnd - paintStart);
}

void DashboardApp::BeginLayoutEditTraceSession(const char* kind, const std::string& detail) {
    layoutEditTraceSession_.Begin(trace_, kind, detail);
}

void DashboardApp::RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) {
    trace_.Timings().Record(trace_, TraceTimingOperationName(phase), elapsed);
    layoutEditTraceSession_.Record(phase, elapsed);
}

void DashboardApp::EndLayoutEditTraceSession(const char* reason) {
    layoutEditTraceSession_.End(trace_, reason);
}
