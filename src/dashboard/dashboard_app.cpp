#include "dashboard/dashboard_app.h"

#include <cmath>
#include <sstream>
#include <string_view>
#include <vector>
#include <wtsapi32.h>

#include "dashboard/dashboard_shell_ui.h"
#include "display/display_config.h"
#include "layout_edit/layout_edit_helpers.h"
#include "layout_edit/layout_edit_tooltip_text.h"
#include "util/localization_catalog.h"
#include "util/trace.h"

namespace {

const UINT kTooltipToolInfoSize = TTTOOLINFOW_V2_SIZE;
constexpr UINT kLayoutEditTooltipFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;

RECT RectFromPoint(RenderPoint point, int radius) {
    return RECT{point.x - radius, point.y - radius, point.x + radius + 1, point.y + radius + 1};
}

constexpr double kScaleEpsilon = 0.0001;

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

std::string EscapeTraceText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string QuoteTraceText(std::string_view text) {
    return "\"" + EscapeTraceText(text) + "\"";
}

std::string FormatTracePoint(POINT point) {
    return std::to_string(point.x) + "," + std::to_string(point.y);
}

std::string FormatTracePoint(RenderPoint point) {
    return std::to_string(point.x) + "," + std::to_string(point.y);
}

}  // namespace

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions)
    : renderer_(trace_), diagnosticsOptions_(diagnosticsOptions), layoutEditController_(*this),
      shellUi_(std::make_unique<DashboardShellUi>(*this)) {}

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

DashboardRenderer& DashboardApp::LayoutEditRenderer() {
    return renderer_;
}

DashboardOverlayState& DashboardApp::LayoutDashboardOverlayState() {
    return rendererDashboardOverlayState_;
}

void DashboardApp::InvalidateLayoutEdit() {
    UpdateLayoutEditTooltip();
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

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    appIconLarge_ = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    appIconSmall_ = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.hIcon = appIconLarge_;
    wc.hIconSm = appIconSmall_;
    if (!RegisterClassExW(&wc)) {
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

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"System Telemetry",
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

const std::wstring& DashboardApp::LastError() const {
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
        SetWindowPos(hwnd_, HWND_TOP, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if ((CurrentWindowDpi() != targetDpi || currentDpi_ != targetDpi ||
            !AreScalesEqual(CurrentRenderScale(), targetScale)) &&
        !ApplyWindowDpi(targetDpi)) {
        return;
    }
    SetWindowPos(hwnd_, HWND_TOP, left, top, WindowWidth(), WindowHeight(), SWP_NOACTIVATE);
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
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
}

bool DashboardApp::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
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
    ReleaseFonts();
    UpdateRendererScale(targetScale);
    if (!InitializeFonts()) {
        return false;
    }

    if (suggestedRect != nullptr) {
        const int width = HasExplicitDisplayScale(controller_.State().config.display.scale)
                              ? WindowWidth()
                              : suggestedRect->right - suggestedRect->left;
        const int height = HasExplicitDisplayScale(controller_.State().config.display.scale)
                               ? WindowHeight()
                               : suggestedRect->bottom - suggestedRect->top;
        SetWindowPos(
            hwnd_, nullptr, suggestedRect->left, suggestedRect->top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

bool DashboardApp::WriteDiagnosticsOutputs() {
    return controller_.WriteDiagnosticsOutputs();
}

std::optional<std::filesystem::path> DashboardApp::PromptDiagnosticsSavePath(
    const wchar_t* defaultFileName, const wchar_t* filter, const wchar_t* defaultExtension) const {
    return PromptSavePath(hwnd_, GetWorkingDirectory(), defaultFileName, filter, defaultExtension);
}

void DashboardApp::BringOnTop() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
}

bool DashboardApp::ApplyConfiguredWallpaper() {
    return ::ApplyConfiguredWallpaper(controller_.State().config, trace_);
}

bool DashboardApp::ApplyLayoutGuideWeights(
    const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyLayoutGuideWeights(*this, target, weights);
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

bool DashboardApp::ApplyLayoutEditValue(DashboardRenderer::LayoutEditParameter parameter, double value) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyLayoutEditValue(*this, parameter, value);
    RecordLayoutEditTracePhase(TracePhase::Apply, std::chrono::steady_clock::now() - start);
    return applied;
}

std::optional<int> DashboardApp::EvaluateLayoutWidgetExtentForWeights(const LayoutEditHost::LayoutTarget& target,
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
    trayIcon_.hIcon = appIconSmall_ != nullptr ? appIconSmall_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"System Telemetry");
    return Shell_NotifyIconW(NIM_ADD, &trayIcon_) == TRUE;
}

void DashboardApp::RemoveTrayIcon() {
    if (trayIcon_.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
    }
}

void DashboardApp::StartMoveMode(std::optional<POINT> cursorAnchorClientPoint) {
    if (controller_.State().isEditingLayout) {
        layoutEditController_.CancelInteraction();
    }
    HideLayoutEditTooltip();
    moveCursorAnchorClientPoint_ = cursorAnchorClientPoint;
    suppressMoveStopOnNextLeftButtonUp_ = false;
    controller_.State().isMoving = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    SyncDashboardMoveOverlayState();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StopMoveMode() {
    if (!controller_.State().isMoving) {
        return;
    }
    moveCursorAnchorClientPoint_.reset();
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

    int x = 0;
    int y = 0;
    if (moveCursorAnchorClientPoint_.has_value()) {
        x = cursor.x - moveCursorAnchorClientPoint_->x;
        y = cursor.y - moveCursorAnchorClientPoint_->y;
    } else {
        int cursorOffset = ScaleLogicalToPhysical(24, CurrentWindowDpi());
        cursorOffset = std::max(
            cursorOffset, renderer_.Renderer().TextMetrics().smallText + ScaleLogicalToPhysical(8, CurrentWindowDpi()));

        x = cursor.x - (WindowWidth() / 2);
        y = cursor.y - cursorOffset;
    }
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
    overlayState.relativePosition = RenderPoint{static_cast<int>(movePlacementInfo_.relativePosition.x),
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
    UpdateLayoutEditTooltip();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::RedrawShellNow() {
    UpdateLayoutEditTooltip();
    if (hwnd_ == nullptr) {
        return;
    }
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

MonitorPlacementInfo DashboardApp::GetWindowPlacementInfo() const {
    return hwnd_ != nullptr ? GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale)
                            : movePlacementInfo_;
}

void DashboardApp::ShowError(const std::wstring& message) const {
    MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
}

bool DashboardApp::CreateLayoutEditTooltip() {
    layoutEditTooltipHwnd_ = CreateWindowExW(WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
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

    TOOLINFOW toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.hwnd = hwnd_;
    toolInfo.uId = 1;
    toolInfo.rect = clientRect;
    toolInfo.lpszText = const_cast<LPWSTR>(L"");
    const LRESULT addToolResult =
        SendMessageW(layoutEditTooltipHwnd_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
    const LRESULT activateResult = SendMessageW(layoutEditTooltipHwnd_, TTM_ACTIVATE, TRUE, 0);
    SendMessageW(layoutEditTooltipHwnd_, TTM_SETDELAYTIME, TTDT_INITIAL, 0);
    SendMessageW(layoutEditTooltipHwnd_, TTM_SETDELAYTIME, TTDT_RESHOW, 0);
    SendMessageW(layoutEditTooltipHwnd_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    SendMessageW(layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
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

void DashboardApp::TraceLayoutEditUiEvent(const std::string& event, const std::string& details) const {
    const auto& state = controller_.State();
    if (state.diagnostics == nullptr) {
        return;
    }

    std::string text = event;
    if (!details.empty()) {
        text += " ";
        text += details;
    }
    const std::string uiState = BuildLayoutEditUiTraceState();
    if (!uiState.empty()) {
        text += " ";
        text += uiState;
    }
    state.diagnostics->WriteTraceMarker(text);
}

std::string DashboardApp::BuildLayoutEditUiTraceState() const {
    const auto& state = controller_.State();
    std::ostringstream trace;
    trace << "layout=" << QuoteTraceText(state.config.display.layout);
    trace << " editing=" << Trace::BoolText(state.isEditingLayout);
    trace << " moving=" << Trace::BoolText(state.isMoving);
    trace << " modal_depth=" << layoutEditModalUiDepth_;
    trace << " tooltip_visible=" << Trace::BoolText(layoutEditTooltipVisible_);
    trace << " tooltip_suppressed=" << Trace::BoolText(layoutEditTooltipRefreshSuppressed_);
    trace << " tooltip_rect_valid=" << Trace::BoolText(layoutEditTooltipRectValid_);
    trace << " mouse_tracking=" << Trace::BoolText(layoutEditMouseTracking_);
    trace << " drag_active=" << Trace::BoolText(layoutEditController_.HasActiveDrag());

    const HWND capture = GetCapture();
    trace << " capture=";
    if (capture == nullptr) {
        trace << QuoteTraceText("none");
    } else if (capture == hwnd_) {
        trace << QuoteTraceText("dashboard");
    } else {
        trace << QuoteTraceText("other");
    }

    if (const auto target = const_cast<LayoutEditController&>(layoutEditController_).CurrentTooltipTarget();
        target.has_value()) {
        trace << " target=" << QuoteTraceText(LayoutEditTooltipPayloadTraceKind(target->payload));
        if (target->clientPoint.has_value()) {
            trace << " target_point=" << QuoteTraceText(FormatTracePoint(*target->clientPoint));
        }
    } else {
        trace << " target=" << QuoteTraceText("none");
    }

    POINT cursor{};
    if (GetCursorPos(&cursor) != FALSE) {
        trace << " cursor_screen=" << QuoteTraceText(FormatTracePoint(cursor));
        if (hwnd_ != nullptr) {
            POINT clientPoint = cursor;
            ScreenToClient(hwnd_, &clientPoint);
            trace << " cursor_client=" << QuoteTraceText(FormatTracePoint(clientPoint));
        }
    }
    return trace.str();
}

void DashboardApp::HideLayoutEditTooltip() {
    if (layoutEditTooltipHwnd_ == nullptr || !layoutEditTooltipVisible_) {
        return;
    }

    TOOLINFOW toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = hwnd_;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.uId = 1;
    SendMessageW(layoutEditTooltipHwnd_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&toolInfo));
    TraceLayoutEditUiEvent("layout_edit_tooltip:hide");
    layoutEditTooltipVisible_ = false;
    layoutEditTooltipRectValid_ = false;
}

void DashboardApp::SetLayoutEditTooltipRefreshSuppressed(bool suppressed) {
    if (layoutEditTooltipRefreshSuppressed_ == suppressed) {
        return;
    }
    layoutEditTooltipRefreshSuppressed_ = suppressed;
    TraceLayoutEditUiEvent("layout_edit_tooltip:suppression", "value=" + QuoteTraceText(suppressed ? "true" : "false"));
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
        TraceLayoutEditUiEvent("layout_edit_hover:suspend", "reason=" + QuoteTraceText("inactive_or_drag"));
        HideLayoutEditTooltip();
        return;
    }

    TraceLayoutEditUiEvent("layout_edit_hover:suspend", "reason=" + QuoteTraceText("covered_pointer"));
    layoutEditController_.HandleMouseLeave();
    HideLayoutEditTooltip();
}

void DashboardApp::UpdateLayoutEditTooltip() {
    if (layoutEditTooltipRefreshSuppressed_) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:update_skip", "reason=" + QuoteTraceText("suppressed"));
        HideLayoutEditTooltip();
        return;
    }
    if (layoutEditTooltipHwnd_ == nullptr || !controller_.State().isEditingLayout || controller_.State().isMoving ||
        shellUi_->IsLayoutEditModalUiActive()) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:update_skip", "reason=" + QuoteTraceText("inactive"));
        HideLayoutEditTooltip();
        return;
    }
    if (layoutEditController_.HasActiveDrag()) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:update_skip", "reason=" + QuoteTraceText("active_drag"));
        HideLayoutEditTooltip();
        return;
    }

    POINT screenPoint{};
    if (GetCursorPos(&screenPoint) && ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:update_skip", "reason=" + QuoteTraceText("covered_pointer"));
        SuspendCoveredLayoutEditHover();
        return;
    }

    const bool wasVisible = layoutEditTooltipVisible_;
    const std::wstring previousText = layoutEditTooltipText_;
    const RECT previousRect = layoutEditTooltipRect_;
    const bool previousRectValid = layoutEditTooltipRectValid_;
    const auto target = layoutEditController_.CurrentTooltipTarget();
    if (!target.has_value()) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:update_skip", "reason=" + QuoteTraceText("no_target"));
        HideLayoutEditTooltip();
        return;
    }

    const RenderPoint clientPoint = target->clientPoint.value_or(TooltipPayloadAnchorPoint(target->payload));
    std::string tooltipError;
    const auto tooltipText =
        BuildLayoutEditTooltipTextForPayload(controller_.State().config, target->payload, &tooltipError);
    if (!tooltipText.has_value()) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:update_abort",
            "reason=" + QuoteTraceText(tooltipError.empty() ? "unsupported_target" : tooltipError));
        HideLayoutEditTooltip();
        return;
    }
    layoutEditTooltipText_ = *tooltipText;

    const int tooltipOffsetX = ScaleLogicalToPhysical(28, CurrentWindowDpi());
    const int tooltipOffsetY = ScaleLogicalToPhysical(24, CurrentWindowDpi());
    const int tooltipRadius = ScaleLogicalToPhysical(10, CurrentWindowDpi());
    layoutEditTooltipRect_ = RectFromPoint(clientPoint, tooltipRadius);
    layoutEditTooltipRectValid_ = true;

    TOOLINFOW toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = hwnd_;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.uId = 1;
    toolInfo.rect = layoutEditTooltipRect_;
    toolInfo.lpszText =
        layoutEditTooltipText_.empty() ? const_cast<LPWSTR>(L"") : const_cast<LPWSTR>(layoutEditTooltipText_.c_str());
    SendMessageW(layoutEditTooltipHwnd_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
    SendMessageW(layoutEditTooltipHwnd_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&toolInfo));
    SendMessageW(layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
    POINT tooltipScreenPoint{clientPoint.x + tooltipOffsetX, clientPoint.y + tooltipOffsetY};
    ClientToScreen(hwnd_, &tooltipScreenPoint);
    SendMessageW(layoutEditTooltipHwnd_, TTM_TRACKPOSITION, 0, MAKELPARAM(tooltipScreenPoint.x, tooltipScreenPoint.y));
    SendMessageW(layoutEditTooltipHwnd_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&toolInfo));

    MSG msg{};
    msg.hwnd = hwnd_;
    msg.message = WM_MOUSEMOVE;
    msg.wParam = 0;
    msg.lParam = MAKELPARAM(clientPoint.x, clientPoint.y);
    SendMessageW(layoutEditTooltipHwnd_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
    SendMessageW(layoutEditTooltipHwnd_, TTM_UPDATE, 0, 0);
    ShowWindow(layoutEditTooltipHwnd_, SW_SHOWNOACTIVATE);
    layoutEditTooltipVisible_ = true;

    if (!wasVisible || previousText != layoutEditTooltipText_ || previousRectValid != layoutEditTooltipRectValid_ ||
        !RectsEqual(previousRect, layoutEditTooltipRect_)) {
        TraceLayoutEditUiEvent("layout_edit_tooltip:show",
            "payload=" + QuoteTraceText(LayoutEditTooltipPayloadTraceKind(target->payload)) +
                " client_point=" + QuoteTraceText(FormatTracePoint(clientPoint)) +
                " text=" + QuoteTraceText(Utf8FromWide(layoutEditTooltipText_)));
    }
}

void DashboardApp::RefreshLayoutEditHoverFromCursor() {
    TraceLayoutEditUiEvent("layout_edit_hover:refresh_begin");
    if (layoutEditTooltipRefreshSuppressed_) {
        TraceLayoutEditUiEvent("layout_edit_hover:refresh_skip", "reason=" + QuoteTraceText("suppressed"));
        HideLayoutEditTooltip();
        return;
    }
    if (!controller_.State().isEditingLayout || controller_.State().isMoving || shellUi_ == nullptr ||
        shellUi_->IsLayoutEditModalUiActive()) {
        TraceLayoutEditUiEvent("layout_edit_hover:refresh_skip", "reason=" + QuoteTraceText("inactive"));
        HideLayoutEditTooltip();
        return;
    }
    if (layoutEditController_.HasActiveDrag()) {
        TraceLayoutEditUiEvent("layout_edit_hover:refresh_skip", "reason=" + QuoteTraceText("active_drag"));
        return;
    }

    POINT screenPoint{};
    if (!GetCursorPos(&screenPoint) || ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
        TraceLayoutEditUiEvent("layout_edit_hover:refresh_skip", "reason=" + QuoteTraceText("covered_or_no_cursor"));
        SuspendCoveredLayoutEditHover();
        return;
    }

    POINT clientPointWin32 = screenPoint;
    ScreenToClient(hwnd_, &clientPointWin32);
    RECT clientRect{};
    GetClientRect(hwnd_, &clientRect);
    if (!PtInRect(&clientRect, clientPointWin32)) {
        TraceLayoutEditUiEvent("layout_edit_hover:refresh_skip", "reason=" + QuoteTraceText("cursor_outside_client"));
        layoutEditController_.HandleMouseLeave();
        HideLayoutEditTooltip();
        return;
    }

    UpdateLayoutEditMouseTracking();
    layoutEditController_.HandleMouseMove(RenderPoint{clientPointWin32.x, clientPointWin32.y});
    TraceLayoutEditUiEvent(
        "layout_edit_hover:refresh_move", "client_point=" + QuoteTraceText(FormatTracePoint(clientPointWin32)));
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
        TraceLayoutEditUiEvent("layout_edit_mouse_tracking:start");
    } else {
        TraceLayoutEditUiEvent("layout_edit_mouse_tracking:start_failed");
    }
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
    SendMessageW(layoutEditTooltipHwnd_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
}

int DashboardApp::Run() {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (shellUi_ != nullptr && shellUi_->HandleDialogMessage(&msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK DashboardApp::WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<DashboardApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DashboardApp::WndProcThunk));
        app->hwnd_ = hwnd;
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK DashboardApp::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app != nullptr ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
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
            SetTimer(hwnd_, kRefreshTimerId, kRefreshTimerMs, nullptr);
            CreateTrayIcon();
            return 0;
        case WM_TIMER:
            if (wParam == kMoveTimerId) {
                UpdateMoveTracking();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (wParam == kPlacementTimerId) {
                RetryConfigPlacementIfPending();
                return 0;
            }
            if (!controller_.HandleRefreshTimer(*this)) {
                DestroyWindow(hwnd_);
                return 0;
            }
            return 0;
        case WM_ACTIVATE:
            if (shellUi_ != nullptr) {
                TraceLayoutEditUiEvent("layout_edit_ui:wm_activate",
                    "active_state=" + QuoteTraceText(std::to_string(static_cast<int>(LOWORD(wParam)))));
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
            std::optional<LayoutEditController::TooltipTarget> layoutEditTarget;
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
                    layoutEditTarget = layoutEditController_.CurrentTooltipTarget();
                }
            }
            if (state.isMoving) {
                StopMoveMode();
            }
            shellUi_->ShowContextMenu(DashboardShellUi::MenuSource::AppWindow, point, layoutEditTarget);
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
                    shellUi_->SyncLayoutEditDialogSelection(layoutEditController_.CurrentTooltipTarget(), false);
                    UpdateLayoutEditTooltip();
                    return 0;
                }
            }
            break;
        case WM_LBUTTONDBLCLK: {
            if (shellUi_->IsLayoutEditModalUiActive()) {
                return 0;
            }
            std::optional<LayoutEditController::TooltipTarget> layoutEditTarget;
            if (state.isEditingLayout && !state.isMoving) {
                const RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                POINT screenPoint{clientPoint.x, clientPoint.y};
                ClientToScreen(hwnd_, &screenPoint);
                if (ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
                    SuspendCoveredLayoutEditHover();
                    return 0;
                }
                layoutEditController_.HandleMouseMove(clientPoint);
                layoutEditTarget = layoutEditController_.CurrentTooltipTarget();
            }
            shellUi_->InvokeDefaultAction(DashboardShellUi::MenuSource::AppWindow,
                layoutEditTarget,
                POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (controller_.State().isMoving) {
                suppressMoveStopOnNextLeftButtonUp_ = true;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
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
                UpdateLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            layoutEditMouseTracking_ = false;
            if (state.isEditingLayout && !state.isMoving && !shellUi_->IsLayoutEditModalUiActive()) {
                TraceLayoutEditUiEvent("layout_edit_ui:wm_mouseleave");
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
                        TraceLayoutEditUiEvent("layout_edit_ui:wm_mouseleave_ignore",
                            "reason=" + QuoteTraceText("cursor_still_in_client"));
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
                    shellUi_->SyncLayoutEditDialogSelection(layoutEditController_.CurrentTooltipTarget(), false);
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
                TraceLayoutEditUiEvent("layout_edit_ui:wm_capturechanged",
                    "new_owner=" +
                        QuoteTraceText(reinterpret_cast<HWND>(lParam) == nullptr
                                           ? "none"
                                           : (reinterpret_cast<HWND>(lParam) == hwnd_ ? "dashboard" : "other")) +
                        " handled=" + QuoteTraceText(handled ? "true" : "false"));
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
                shellUi_->ShowContextMenu(DashboardShellUi::MenuSource::TrayIcon, point, std::nullopt);
                return 0;
            }
            if (lParam == WM_LBUTTONDBLCLK) {
                shellUi_->InvokeDefaultAction(DashboardShellUi::MenuSource::TrayIcon, std::nullopt);
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
                SendMessageW(
                    layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
            }
            UpdateLayoutEditTooltip();
            movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
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
            KillTimer(hwnd_, kRefreshTimerId);
            KillTimer(hwnd_, kMoveTimerId);
            KillTimer(hwnd_, kPlacementTimerId);
            UnregisterSessionNotifications();
            DestroyLayoutEditTooltip();
            if (state.diagnostics != nullptr) {
                state.diagnostics->WriteTraceMarker("diagnostics:ui_done");
            }
            RemoveTrayIcon();
            ReleaseFonts();
            {
                HICON largeIcon = appIconLarge_;
                HICON smallIcon = appIconSmall_;
                appIconLarge_ = nullptr;
                appIconSmall_ = nullptr;
                if (largeIcon != nullptr) {
                    DestroyIcon(largeIcon);
                }
                if (smallIcon != nullptr && smallIcon != largeIcon) {
                    DestroyIcon(smallIcon);
                }
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void DashboardApp::Paint() {
    const auto paintStart = std::chrono::steady_clock::now();
    PAINTSTRUCT ps{};
    BeginPaint(hwnd_, &ps);
    const SystemSnapshot& snapshot = controller_.State().telemetry->Snapshot();
    const auto drawStart = std::chrono::steady_clock::now();
    SyncDashboardMoveOverlayState();
    renderer_.DrawWindow(snapshot, rendererDashboardOverlayState_);
    const auto drawEnd = std::chrono::steady_clock::now();
    EndPaint(hwnd_, &ps);
    const auto paintEnd = std::chrono::steady_clock::now();
    RecordLayoutEditTracePhase(TracePhase::PaintDraw, drawEnd - drawStart);
    RecordLayoutEditTracePhase(TracePhase::PaintTotal, paintEnd - paintStart);
}

void DashboardApp::BeginLayoutEditTraceSession(const std::string& kind, const std::string& detail) {
    layoutEditTraceSession_.Begin(trace_, kind, detail);
}

void DashboardApp::RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) {
    layoutEditTraceSession_.Record(phase, elapsed);
}

void DashboardApp::EndLayoutEditTraceSession(const std::string& reason) {
    layoutEditTraceSession_.End(trace_, reason);
}
