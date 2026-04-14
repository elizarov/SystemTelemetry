#include "dashboard_app.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "dashboard_shell_ui.h"
#include "layout_edit_service.h"
#include "layout_edit_tooltip.h"
#include "localization_catalog.h"

using layout_edit::LayoutEditAnchorRegion;
using layout_edit::LayoutEditGapAnchor;
using layout_edit::LayoutEditGuide;
using layout_edit::LayoutEditWidgetGuide;
using layout_edit::LayoutEditWidgetIdentity;
using layout_edit::LayoutGuideAxis;

namespace {

constexpr UINT kTooltipToolInfoSize = TTTOOLINFOW_V2_SIZE;
constexpr UINT kLayoutEditTooltipFlags = TTF_SUBCLASS | TTF_TRANSPARENT;

RECT RectFromPoint(RenderPoint point, int radius) {
    return RECT{point.x - radius, point.y - radius, point.x + radius + 1, point.y + radius + 1};
}

constexpr double kScaleEpsilon = 0.0001;

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

std::wstring BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, double value, const std::wstring& descriptionText) {
    std::wstring text = WideFromUtf8(BuildLayoutEditTooltipLine(descriptor, value));
    if (!descriptionText.empty()) {
        text += L"\r\n";
        text += descriptionText;
    }
    return text;
}

std::wstring BuildTooltipText(
    const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value, const std::wstring& descriptionText) {
    std::wstring text = WideFromUtf8(BuildLayoutEditTooltipLine(descriptor, value));
    if (!descriptionText.empty()) {
        text += L"\r\n";
        text += descriptionText;
    }
    return text;
}

std::string LayoutGuideTooltipSectionName(const AppConfig& config, const LayoutEditGuide& guide) {
    if (!guide.editCardId.empty()) {
        return "card." + guide.editCardId;
    }
    if (!config.display.layout.empty()) {
        return "layout." + config.display.layout;
    }
    return "layout";
}

std::string LayoutGuideTooltipConfigMember(const LayoutEditGuide& guide) {
    return guide.editCardId.empty() ? "cards" : "layout";
}

const LayoutNodeConfig* FindLayoutGuideNode(const AppConfig& config, const LayoutEditGuide& guide) {
    return layout_edit::FindGuideNode(config, LayoutEditHost::LayoutTarget::ForGuide(guide));
}

std::string LayoutGuideChildName(const LayoutNodeConfig& node) {
    return node.name.empty() ? "unknown" : node.name;
}

std::string BuildLayoutGuideTooltipLine(const AppConfig& config, const LayoutEditGuide& guide) {
    const std::string sectionName = LayoutGuideTooltipSectionName(config, guide);
    const std::string configMember = LayoutGuideTooltipConfigMember(guide);
    const LayoutNodeConfig* node = FindLayoutGuideNode(config, guide);
    if (node == nullptr || node->children.size() < 2 || guide.separatorIndex + 1 >= node->children.size()) {
        return "[" + sectionName + "] " + configMember;
    }

    const LayoutNodeConfig& leftChild = node->children[guide.separatorIndex];
    const LayoutNodeConfig& rightChild = node->children[guide.separatorIndex + 1];
    return "[" + sectionName + "] " + configMember + " = ... " + node->name + "(" + LayoutGuideChildName(leftChild) +
           ":" + std::to_string(std::max(1, leftChild.weight)) + ", " + LayoutGuideChildName(rightChild) + ":" +
           std::to_string(std::max(1, rightChild.weight)) + ")";
}

std::wstring BuildLayoutGuideTooltipText(const AppConfig& config, const LayoutEditGuide& guide) {
    std::wstring text = WideFromUtf8(BuildLayoutGuideTooltipLine(config, guide));
    const std::wstring description = WideFromUtf8(FindLocalizedText("layout_edit.layout_guide"));
    if (!description.empty()) {
        text += L"\r\n";
        text += description;
    }
    return text;
}

}  // namespace

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions)
    : diagnosticsOptions_(diagnosticsOptions), layoutEditController_(*this),
      shellUi_(std::make_unique<DashboardShellUi>(*this)) {}

DashboardApp::~DashboardApp() = default;

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    controller_.State().config = config;
    renderer_.SetConfig(config);
    rendererEditOverlayState_.showLayoutEditGuides =
        controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
    rendererEditOverlayState_.similarityIndicatorMode = GetSimilarityIndicatorMode(diagnosticsOptions_);
    SyncMoveOverlayState();
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

DashboardRenderer::EditOverlayState& DashboardApp::LayoutEditOverlayState() {
    return rendererEditOverlayState_;
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
    instance_ = instance;
    InitializeLocalizationCatalog();
    if (!controller_.InitializeSession(*this, diagnosticsOptions_)) {
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
    renderer_.SetTraceOutput(
        controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr);
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
    rendererEditOverlayState_.showLayoutEditGuides =
        controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
    SyncMoveOverlayState();
    renderer_.SetTraceOutput(
        controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr);
    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    return renderer_.SaveSnapshotPng(imagePath, snapshot, rendererEditOverlayState_);
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
    return ::ApplyConfiguredWallpaper(controller_.State().config,
        controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr);
}

bool DashboardApp::ApplyLayoutGuideWeights(
    const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    const auto start = std::chrono::steady_clock::now();
    const bool applied = controller_.ApplyLayoutGuideWeights(*this, target, weights);
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
        controller_.StopLayoutEditMode(*this, layoutEditController_, diagnosticsOptions_.editLayout);
    }
    HideLayoutEditTooltip();
    moveCursorAnchorClientPoint_ = cursorAnchorClientPoint;
    suppressMoveStopOnNextLeftButtonUp_ = false;
    controller_.State().isMoving = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    SyncMoveOverlayState();
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
    SyncMoveOverlayState();
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
        cursorOffset =
            std::max(cursorOffset, renderer_.TextMetrics().smallText + ScaleLogicalToPhysical(8, CurrentWindowDpi()));

        x = cursor.x - (WindowWidth() / 2);
        y = cursor.y - cursorOffset;
    }
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
    SyncMoveOverlayState();
}

void DashboardApp::SyncMoveOverlayState() {
    auto& overlayState = rendererEditOverlayState_.moveOverlay;
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

DashboardRenderer& DashboardApp::Renderer() {
    return renderer_;
}

const DashboardRenderer& DashboardApp::Renderer() const {
    return renderer_;
}

DashboardRenderer::EditOverlayState& DashboardApp::RendererEditOverlayState() {
    return rendererEditOverlayState_;
}

const DashboardRenderer::EditOverlayState& DashboardApp::RendererEditOverlayState() const {
    return rendererEditOverlayState_;
}

void DashboardApp::InvalidateShell() {
    UpdateLayoutEditTooltip();
    InvalidateRect(hwnd_, nullptr, FALSE);
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

void DashboardApp::HideLayoutEditTooltip() {
    if (layoutEditTooltipHwnd_ == nullptr || !layoutEditTooltipVisible_) {
        return;
    }

    TOOLINFOW toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = hwnd_;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.uId = 1;
    toolInfo.rect = layoutEditTooltipRect_;
    SendMessageW(layoutEditTooltipHwnd_, TTM_POP, 0, 0);
    layoutEditTooltipVisible_ = false;
    layoutEditTooltipRectValid_ = false;
}

void DashboardApp::UpdateLayoutEditTooltip() {
    if (layoutEditTooltipHwnd_ == nullptr || !controller_.State().isEditingLayout || controller_.State().isMoving ||
        shellUi_->IsLayoutEditModalUiActive()) {
        HideLayoutEditTooltip();
        return;
    }

    const auto target = layoutEditController_.CurrentTooltipTarget();
    if (!target.has_value()) {
        HideLayoutEditTooltip();
        return;
    }

    std::optional<LayoutEditTooltipDescriptor> descriptor;
    double value = 0.0;
    std::optional<UiFontConfig> fontValue;
    const RenderPoint clientPoint = target->clientPoint.value_or(layout_edit::TooltipPayloadAnchorPoint(target->payload));
    if (const auto* guide = std::get_if<LayoutEditGuide>(&target->payload)) {
        layoutEditTooltipText_ = BuildLayoutGuideTooltipText(controller_.State().config, *guide);
    } else {
        if (const auto parameter = layout_edit::TooltipPayloadParameter(target->payload); parameter.has_value()) {
            descriptor = FindLayoutEditTooltipDescriptor(*parameter);
            value = layout_edit::TooltipPayloadNumericValue(target->payload).value_or(0.0);
            if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&target->payload)) {
                if (const auto currentFont =
                        FindLayoutEditTooltipFontValue(controller_.State().config, anchor->key.parameter);
                    currentFont.has_value() && *currentFont != nullptr) {
                    fontValue = **currentFont;
                }
            }
        }
    }

    if (!layout_edit::IsLayoutGuidePayload(target->payload) && !descriptor.has_value()) {
        HideLayoutEditTooltip();
        return;
    }

    if (!layout_edit::IsLayoutGuidePayload(target->payload)) {
        const std::wstring description = WideFromUtf8(FindLocalizedText(descriptor->configKey));
        layoutEditTooltipText_ = descriptor->valueFormat == configschema::ValueFormat::FontSpec && fontValue.has_value()
                                     ? BuildTooltipText(*descriptor, *fontValue, description)
                                     : BuildTooltipText(*descriptor, value, description);
    }

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

    MSG msg{};
    msg.hwnd = hwnd_;
    msg.message = WM_MOUSEMOVE;
    msg.wParam = 0;
    msg.lParam = MAKELPARAM(clientPoint.x, clientPoint.y);
    SendMessageW(layoutEditTooltipHwnd_, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&msg));
    SendMessageW(layoutEditTooltipHwnd_, TTM_UPDATE, 0, 0);
    ShowWindow(layoutEditTooltipHwnd_, SW_SHOWNOACTIVATE);
    layoutEditTooltipVisible_ = true;
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
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            std::optional<LayoutEditController::TooltipTarget> layoutEditTarget;
            if (point.x == -1 && point.y == -1) {
                RECT rect{};
                GetWindowRect(hwnd_, &rect);
                point.x = rect.left + 24;
                point.y = rect.top + 24;
            } else if (state.isEditingLayout && !state.isMoving) {
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
            if (state.isEditingLayout && !shellUi_->IsLayoutEditModalUiActive()) {
                RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (layoutEditController_.HandleLButtonDown(hwnd_, clientPoint)) {
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
            if (state.isEditingLayout && !shellUi_->IsLayoutEditModalUiActive()) {
                UpdateLayoutEditMouseTracking();
                RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                layoutEditController_.HandleMouseMove(clientPoint);
                UpdateLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            layoutEditMouseTracking_ = false;
            if (state.isEditingLayout && !shellUi_->IsLayoutEditModalUiActive()) {
                POINT screenPoint{};
                if (GetCursorPos(&screenPoint)) {
                    POINT clientPointWin32 = screenPoint;
                    ScreenToClient(hwnd_, &clientPointWin32);
                    RECT clientRect{};
                    GetClientRect(hwnd_, &clientRect);
                    if (PtInRect(&clientRect, clientPointWin32)) {
                        const RenderPoint clientPoint{clientPointWin32.x, clientPointWin32.y};
                        UpdateLayoutEditMouseTracking();
                        layoutEditController_.HandleMouseMove(clientPoint);
                        UpdateLayoutEditTooltip();
                        return 0;
                    }
                }
                layoutEditController_.HandleMouseLeave();
                HideLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (state.isEditingLayout && !shellUi_->IsLayoutEditModalUiActive()) {
                RenderPoint clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (layoutEditController_.HandleLButtonUp(clientPoint)) {
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
                if (state.isEditingLayout) {
                    controller_.StopLayoutEditMode(*this, layoutEditController_, diagnosticsOptions_.editLayout);
                    HideLayoutEditTooltip();
                    return 0;
                }
                if (state.isMoving) {
                    StopMoveMode();
                    return 0;
                }
            }
            break;
        case WM_CAPTURECHANGED:
            if (state.isEditingLayout && !shellUi_->IsLayoutEditModalUiActive() &&
                layoutEditController_.HandleCaptureChanged(hwnd_, reinterpret_cast<HWND>(lParam))) {
                UpdateLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && state.isEditingLayout && !shellUi_->IsLayoutEditModalUiActive()) {
                layoutEditController_.HandleSetCursor(hwnd_);
                return TRUE;
            }
            break;
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
            StartPlacementWatch();
            RetryConfigPlacementIfPending();
            controller_.RefreshTelemetrySelections(*this);
            if (!ApplyWindowDpi(CurrentWindowDpi())) {
                return -1;
            }
            movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_DEVICECHANGE:
        case WM_SETTINGCHANGE:
            StartPlacementWatch();
            RetryConfigPlacementIfPending();
            controller_.RefreshTelemetrySelections(*this);
            movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            KillTimer(hwnd_, kRefreshTimerId);
            KillTimer(hwnd_, kMoveTimerId);
            KillTimer(hwnd_, kPlacementTimerId);
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
    SyncMoveOverlayState();
    renderer_.DrawWindow(snapshot, rendererEditOverlayState_);
    const auto drawEnd = std::chrono::steady_clock::now();
    EndPaint(hwnd_, &ps);
    const auto paintEnd = std::chrono::steady_clock::now();
    RecordLayoutEditTracePhase(TracePhase::PaintDraw, drawEnd - drawStart);
    RecordLayoutEditTracePhase(TracePhase::PaintTotal, paintEnd - paintStart);
}

void DashboardApp::BeginLayoutEditTraceSession(const std::string& kind, const std::string& detail) {
    layoutEditTraceSession_.Begin(
        controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr,
        kind,
        detail);
}

void DashboardApp::RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) {
    layoutEditTraceSession_.Record(phase, elapsed);
}

void DashboardApp::EndLayoutEditTraceSession(const std::string& reason) {
    layoutEditTraceSession_.End(
        controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr, reason);
}
