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
#include "util/text_format.h"
#include "util/trace.h"
#include "widget/app_icon_geometry.h"

namespace {

RECT RectFromPoint(RenderPoint point, int radius) {
    return RECT{point.x - radius, point.y - radius, point.x + radius + 1, point.y + radius + 1};
}

constexpr double kScaleEpsilon = 0.0001;
constexpr int kBringToFrontRetryCount = 8;
constexpr char kTitlebarProbeWindowClassName[] = "CaseDashDashboardTitlebarProbe";
constexpr DWORD kDashboardHiddenWindowStyle = WS_POPUP;
constexpr DWORD kDashboardVisibleTitlebarStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
constexpr DWORD kDashboardTitlebarStyleMask = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
constexpr BYTE kTitlebarProbeAlpha = 1;
constexpr BYTE kTitlebarVisibleAlpha = 255;
constexpr int kTitlebarLayoutComboId = 4201;
constexpr int kTitlebarThemeComboId = 4202;
constexpr int kTitlebarHorizontalPaddingLogical = 12;
constexpr int kTitlebarControlGapLogical = 6;
constexpr int kTitlebarLayoutComboWidthLogical = 78;
constexpr int kTitlebarThemeComboWidthLogical = 112;
constexpr int kTitlebarComboHeightLogical = 22;
constexpr int kTitlebarComboVisibleRows = 8;
constexpr int kTitlebarComboDropPaddingLogical = 6;

using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

int RectWidth(const RECT& rect) {
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect) {
    return rect.bottom - rect.top;
}

bool IsRectUsable(const RECT& rect) {
    return RectWidth(rect) > 0 && RectHeight(rect) > 0;
}

bool TitlebarRectsEqual(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top && left.right == right.right && left.bottom == right.bottom;
}

int NativeTitlebarGlyphSize(UINT dpi) {
    return std::max(8, ScaleLogicalToPhysical(10, dpi));
}

int NativeTitlebarAppIconSize(UINT dpi) {
    return std::max(16, ScaleLogicalToPhysical(16, dpi));
}

std::string TitlebarThemeDisplayName(std::string_view name) {
    std::string result{name};
    std::replace(result.begin(), result.end(), '_', ' ');
    return result;
}

bool AdjustDashboardWindowRectForDpi(RECT& rect, DWORD style, DWORD exStyle, UINT dpi) {
    static AdjustWindowRectExForDpiFn adjustWindowRectExForDpi = []() -> AdjustWindowRectExForDpiFn {
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (user32 == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<AdjustWindowRectExForDpiFn>(GetProcAddress(user32, "AdjustWindowRectExForDpi"));
    }();

    if (adjustWindowRectExForDpi != nullptr) {
        return adjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi) != FALSE;
    }
    return AdjustWindowRectEx(&rect, style, FALSE, exStyle) != FALSE;
}

void FillRectWithColor(HDC hdc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

COLORREF ColorConfigToColorRef(const ColorConfig& color) {
    const unsigned int rgb = color.ToRgb();
    return RGB(
        static_cast<BYTE>((rgb >> 16) & 0xFFu), static_cast<BYTE>((rgb >> 8) & 0xFFu), static_cast<BYTE>(rgb & 0xFFu));
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

    WNDCLASSEXA probeClass{};
    probeClass.cbSize = sizeof(probeClass);
    probeClass.lpfnWndProc = &DashboardApp::TitlebarProbeWndProcSetup;
    probeClass.hInstance = instance;
    probeClass.lpszClassName = kTitlebarProbeWindowClassName;
    probeClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    probeClass.hbrBackground = nullptr;
    if (!RegisterClassExA(&probeClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
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

    hwnd_ = CreateWindowExA(WS_EX_TOOLWINDOW,
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
    return CreateDashboardTooltip() && CreateNativeTitlebarProbe();
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
        const RECT targetClientRect{left, top, left + WindowWidth(), top + WindowHeight()};
        const RECT targetWindowRect = ResolveWindowRectForDashboardClientRect(targetClientRect);
        SetWindowPos(hwnd_,
            nullptr,
            targetWindowRect.left,
            targetWindowRect.top,
            0,
            0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
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

    RECT currentClientRect = DashboardClientScreenRect();
    const bool sizeChanged = RectWidth(currentClientRect) != width || RectHeight(currentClientRect) != height;
    if (sizeChanged) {
        renderer_.DiscardWindowRenderTarget(reason);
        // Surface changes render a fresh frame explicitly; do not let USER32 preserve old client pixels.
        flags |= SWP_NOREDRAW | SWP_NOCOPYBITS;
    }

    const RECT targetClientRect{left, top, left + width, top + height};
    const RECT targetWindowRect = ResolveWindowRectForDashboardClientRect(targetClientRect);
    SetWindowPos(hwnd_,
        nullptr,
        targetWindowRect.left,
        targetWindowRect.top,
        RectWidth(targetWindowRect),
        RectHeight(targetWindowRect),
        flags);
    if (sizeChanged) {
        RedrawDashboardSurfaceSynchronously();
    }
    UpdateNativeTitlebarProbe();
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
    movePlacementInfo_ =
        GetMonitorPlacementForRect(DashboardClientScreenRect(), controller_.State().config.display.scale);
    UpdateNativeTitlebarHoverFromCursor();
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
        movePlacementInfo_ =
            GetMonitorPlacementForRect(DashboardClientScreenRect(), controller_.State().config.display.scale);
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
        int clientLeft = suggestedRect->left;
        int clientTop = suggestedRect->top;
        int suggestedClientWidth = suggestedRect->right - suggestedRect->left;
        int suggestedClientHeight = suggestedRect->bottom - suggestedRect->top;
        if (nativeTitlebarVisible_) {
            const DashboardTitlebarFrameMargins margins =
                ComputeNativeTitlebarFrameMargins(WindowWidth(), WindowHeight());
            clientLeft += margins.left;
            clientTop += margins.top;
            suggestedClientWidth = std::max(0, suggestedClientWidth - margins.left - margins.right);
            suggestedClientHeight = std::max(0, suggestedClientHeight - margins.top - margins.bottom);
        }
        const int width =
            HasExplicitDisplayScale(controller_.State().config.display.scale) ? WindowWidth() : suggestedClientWidth;
        const int height =
            HasExplicitDisplayScale(controller_.State().config.display.scale) ? WindowHeight() : suggestedClientHeight;
        SetDashboardWindowGeometry(clientLeft, clientTop, width, height, SWP_NOZORDER | SWP_NOACTIVATE, "dpi_change");
    }
    return true;
}

RECT DashboardApp::DashboardClientScreenRect() const {
    RECT clientRect{};
    if (hwnd_ == nullptr) {
        return clientRect;
    }
    GetClientRect(hwnd_, &clientRect);
    POINT topLeft{clientRect.left, clientRect.top};
    POINT bottomRight{clientRect.right, clientRect.bottom};
    ClientToScreen(hwnd_, &topLeft);
    ClientToScreen(hwnd_, &bottomRight);
    return RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

DashboardTitlebarFrameMargins DashboardApp::ComputeNativeTitlebarFrameMargins(int clientWidth, int clientHeight) const {
    RECT adjustedRect{0, 0, clientWidth, clientHeight};
    const DWORD exStyle =
        hwnd_ != nullptr ? static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_EXSTYLE)) : WS_EX_TOOLWINDOW;
    if (!AdjustDashboardWindowRectForDpi(adjustedRect, kDashboardVisibleTitlebarStyle, exStyle, CurrentWindowDpi())) {
        return {};
    }
    return DashboardTitlebarFrameMarginsFromAdjustedRect(adjustedRect, clientWidth, clientHeight);
}

DashboardTitlebarGeometry DashboardApp::ResolveNativeTitlebarGeometry(const RECT& dashboardClientRect) const {
    if (!IsRectUsable(dashboardClientRect)) {
        return {};
    }
    const MonitorPlacementInfo placement =
        GetMonitorPlacementForRect(dashboardClientRect, controller_.State().config.display.scale);
    const DashboardTitlebarFrameMargins margins =
        ComputeNativeTitlebarFrameMargins(RectWidth(dashboardClientRect), RectHeight(dashboardClientRect));
    return ResolveDashboardTitlebarGeometry(dashboardClientRect, placement.monitorRect, margins);
}

RECT DashboardApp::ResolveWindowRectForDashboardClientRect(const RECT& dashboardClientRect) const {
    if (!nativeTitlebarVisible_) {
        return dashboardClientRect;
    }

    const DashboardTitlebarFrameMargins margins =
        ComputeNativeTitlebarFrameMargins(RectWidth(dashboardClientRect), RectHeight(dashboardClientRect));
    return RECT{dashboardClientRect.left - margins.left,
        dashboardClientRect.top - margins.top,
        dashboardClientRect.right + margins.right,
        dashboardClientRect.bottom + margins.bottom};
}

void DashboardApp::StartNativeTitlebarHoverTimer() {
    if (hwnd_ == nullptr || nativeTitlebarHoverTimerActive_) {
        return;
    }
    SetTimer(hwnd_, kTitlebarHoverTimerId, kTitlebarHoverTimerMs, nullptr);
    nativeTitlebarHoverTimerActive_ = true;
}

void DashboardApp::StopNativeTitlebarHoverTimer() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kTitlebarHoverTimerId);
    }
    nativeTitlebarHoverTimerActive_ = false;
}

bool DashboardApp::CreateNativeTitlebarProbe() {
    titlebarHoverProbeHwnd_ = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kTitlebarProbeWindowClassName,
        "",
        WS_POPUP | WS_CLIPCHILDREN,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        this);
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return false;
    }
    SetLayeredWindowAttributes(titlebarHoverProbeHwnd_, 0, kTitlebarProbeAlpha, LWA_ALPHA);
    nativeTitlebarProbeAlpha_ = kTitlebarProbeAlpha;
    if (!CreateNativeTitlebarControls()) {
        DestroyNativeTitlebarProbe();
        return false;
    }
    UpdateNativeTitlebarProbe();
    return true;
}

void DashboardApp::DestroyNativeTitlebarProbe() {
    DestroyNativeTitlebarControls();
    ClearNativeTitlebarProbeRegion();
    if (titlebarHoverProbeHwnd_ != nullptr) {
        DestroyWindow(titlebarHoverProbeHwnd_);
        titlebarHoverProbeHwnd_ = nullptr;
    }
    nativeTitlebarHoverInside_ = false;
    nativeTitlebarProbeVisible_ = false;
    nativeTitlebarProbeRectValid_ = false;
}

void DashboardApp::ClearNativeTitlebarProbeRegion() {
    if (titlebarHoverProbeHwnd_ != nullptr && nativeTitlebarProbeRounded_) {
        SetWindowRgn(titlebarHoverProbeHwnd_, nullptr, TRUE);
    }
    nativeTitlebarProbeRounded_ = false;
    nativeTitlebarProbeRegionWidth_ = 0;
    nativeTitlebarProbeRegionHeight_ = 0;
}

void DashboardApp::UpdateNativeTitlebarProbeRegion(int width, int height) {
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return;
    }
    if (!nativeTitlebarVisible_ || width <= 0 || height <= 0) {
        ClearNativeTitlebarProbeRegion();
        return;
    }
    if (nativeTitlebarProbeRounded_ && nativeTitlebarProbeRegionWidth_ == width &&
        nativeTitlebarProbeRegionHeight_ == height) {
        return;
    }

    const int radius = std::clamp(ResolveDashboardTitlebarCornerRadius(CurrentWindowDpi()), 1, std::min(width, height));
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + radius + 1, radius * 2, radius * 2);
    if (region == nullptr) {
        return;
    }
    if (SetWindowRgn(titlebarHoverProbeHwnd_, region, TRUE) == 0) {
        DeleteObject(region);
        return;
    }
    nativeTitlebarProbeRounded_ = true;
    nativeTitlebarProbeRegionWidth_ = width;
    nativeTitlebarProbeRegionHeight_ = height;
}

void DashboardApp::UpdateNativeTitlebarProbe() {
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return;
    }
    if (hwnd_ == nullptr || !IsWindowVisible(hwnd_) ||
        (!nativeTitlebarVisible_ && (controller_.State().isMoving || layoutEditModalUiDepth_ > 0))) {
        if (nativeTitlebarProbeVisible_) {
            ShowWindow(titlebarHoverProbeHwnd_, SW_HIDE);
        }
        ClearNativeTitlebarProbeRegion();
        nativeTitlebarHoverInside_ = false;
        nativeTitlebarProbeVisible_ = false;
        nativeTitlebarProbeRectValid_ = false;
        ShowNativeTitlebarControls(false);
        ResetNativeTitlebarButtonState();
        return;
    }

    const RECT dashboardClientRect = DashboardClientScreenRect();
    DashboardTitlebarGeometry geometry = ResolveNativeTitlebarGeometry(dashboardClientRect);
    if (!geometry.canShow && nativeTitlebarDragMoveActive_ && controller_.State().isMoving && nativeTitlebarVisible_) {
        // Preserve the custom strip during an active titlebar drag even after hover eligibility no longer fits.
        const DashboardTitlebarFrameMargins margins =
            ComputeNativeTitlebarFrameMargins(RectWidth(dashboardClientRect), RectHeight(dashboardClientRect));
        geometry = ResolveDashboardTitlebarFrameGeometry(dashboardClientRect, margins);
    }
    if (!geometry.canShow) {
        if (nativeTitlebarProbeVisible_) {
            ShowWindow(titlebarHoverProbeHwnd_, SW_HIDE);
        }
        ClearNativeTitlebarProbeRegion();
        nativeTitlebarProbeVisible_ = false;
        nativeTitlebarProbeRectValid_ = false;
        ShowNativeTitlebarControls(false);
        ResetNativeTitlebarButtonState();
        return;
    }

    const BYTE targetAlpha = nativeTitlebarVisible_ ? kTitlebarVisibleAlpha : kTitlebarProbeAlpha;
    const bool alphaChanged = nativeTitlebarProbeAlpha_ != targetAlpha;
    if (alphaChanged) {
        SetLayeredWindowAttributes(titlebarHoverProbeHwnd_, 0, targetAlpha, LWA_ALPHA);
        nativeTitlebarProbeAlpha_ = targetAlpha;
    }

    const bool rectChanged =
        !nativeTitlebarProbeRectValid_ || !TitlebarRectsEqual(nativeTitlebarProbeRect_, geometry.virtualHoverRect);
    const bool wasVisible = nativeTitlebarProbeVisible_;
    const int width = RectWidth(geometry.virtualHoverRect);
    const int height = RectHeight(geometry.virtualHoverRect);
    if (rectChanged || !wasVisible) {
        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
        if (wasVisible) {
            flags |= SWP_NOZORDER;
        }
        SetWindowPos(titlebarHoverProbeHwnd_,
            wasVisible ? nullptr : HWND_TOP,
            geometry.virtualHoverRect.left,
            geometry.virtualHoverRect.top,
            width,
            height,
            flags);
        nativeTitlebarProbeRect_ = geometry.virtualHoverRect;
        nativeTitlebarProbeRectValid_ = true;
    }
    UpdateNativeTitlebarProbeRegion(width, height);
    if (!wasVisible) {
        ShowWindow(titlebarHoverProbeHwnd_, SW_SHOWNOACTIVATE);
    }
    nativeTitlebarProbeVisible_ = true;
    if (nativeTitlebarVisible_) {
        UpdateNativeTitlebarControls();
        if (!wasVisible || rectChanged || alphaChanged) {
            InvalidateRect(titlebarHoverProbeHwnd_, nullptr, FALSE);
        }
    } else {
        ShowNativeTitlebarControls(false);
        ResetNativeTitlebarButtonState();
    }
}

void DashboardApp::ShowNativeTitlebar(const DashboardTitlebarGeometry& geometry) {
    if (hwnd_ == nullptr || nativeTitlebarVisible_ || !geometry.canShow) {
        return;
    }

    const LONG_PTR currentStyle = GetWindowLongPtrA(hwnd_, GWL_STYLE);
    const LONG_PTR nextStyle =
        (currentStyle & ~static_cast<LONG_PTR>(kDashboardTitlebarStyleMask)) | kDashboardVisibleTitlebarStyle;
    SetWindowLongPtrA(hwnd_, GWL_STYLE, nextStyle);
    nativeTitlebarVisible_ = true;
    RefreshNativeTitlebarChrome();
    if (nativeTitlebarProbeVisible_) {
        ShowWindow(titlebarHoverProbeHwnd_, SW_HIDE);
        nativeTitlebarProbeVisible_ = false;
        nativeTitlebarProbeRectValid_ = false;
    }

    SetWindowPos(hwnd_,
        nullptr,
        geometry.windowRect.left,
        geometry.windowRect.top,
        RectWidth(geometry.windowRect),
        RectHeight(geometry.windowRect),
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    UpdateNativeTitlebarProbe();
    StartNativeTitlebarHoverTimer();
}

void DashboardApp::HideNativeTitlebar() {
    if (hwnd_ == nullptr || !nativeTitlebarVisible_) {
        UpdateNativeTitlebarProbe();
        return;
    }

    const RECT clientRect = DashboardClientScreenRect();
    const LONG_PTR currentStyle = GetWindowLongPtrA(hwnd_, GWL_STYLE);
    const LONG_PTR nextStyle =
        (currentStyle & ~static_cast<LONG_PTR>(kDashboardTitlebarStyleMask)) | kDashboardHiddenWindowStyle;
    SetWindowLongPtrA(hwnd_, GWL_STYLE, nextStyle);
    nativeTitlebarVisible_ = false;
    RefreshNativeTitlebarChrome();
    HideTitlebarTooltip();
    ShowNativeTitlebarControls(false);
    ResetNativeTitlebarButtonState();
    SetWindowPos(hwnd_,
        nullptr,
        clientRect.left,
        clientRect.top,
        RectWidth(clientRect),
        RectHeight(clientRect),
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    UpdateNativeTitlebarProbe();
}

bool DashboardApp::CreateNativeTitlebarControls() {
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return false;
    }
    if (titlebarLayoutComboHwnd_ != nullptr && titlebarThemeComboHwnd_ != nullptr) {
        return true;
    }

    auto createCombo = [&](int id) -> HWND {
        HWND combo = CreateWindowExA(0,
            WC_COMBOBOXA,
            "",
            WS_CHILD | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
            0,
            0,
            0,
            0,
            titlebarHoverProbeHwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        if (combo != nullptr) {
            SendMessageA(combo, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            SendMessageA(combo, CB_SETMINVISIBLE, 8, 0);
        }
        return combo;
    };

    titlebarLayoutComboHwnd_ = createCombo(kTitlebarLayoutComboId);
    titlebarThemeComboHwnd_ = createCombo(kTitlebarThemeComboId);
    if (titlebarLayoutComboHwnd_ == nullptr || titlebarThemeComboHwnd_ == nullptr) {
        DestroyNativeTitlebarControls();
        return false;
    }

    SyncNativeTitlebarControls();
    UpdateNativeTitlebarControls();
    return true;
}

void DashboardApp::DestroyNativeTitlebarControls() {
    HideTitlebarTooltip();
    nativeTitlebarComboDropdownOpen_ = false;
    nativeTitlebarControlsVisible_ = false;
    if (titlebarLayoutComboHwnd_ != nullptr) {
        DestroyWindow(titlebarLayoutComboHwnd_);
        titlebarLayoutComboHwnd_ = nullptr;
    }
    if (titlebarThemeComboHwnd_ != nullptr) {
        DestroyWindow(titlebarThemeComboHwnd_);
        titlebarThemeComboHwnd_ = nullptr;
    }
    nativeTitlebarLayoutItems_.clear();
    nativeTitlebarThemeItems_.clear();
    nativeTitlebarSelectedLayout_.clear();
    nativeTitlebarSelectedTheme_.clear();
}

void DashboardApp::SyncNativeTitlebarControls() {
    if (titlebarLayoutComboHwnd_ == nullptr || titlebarThemeComboHwnd_ == nullptr) {
        return;
    }

    const DashboardSessionState& state = controller_.State();
    PopulateNativeTitlebarCombo(titlebarLayoutComboHwnd_,
        NativeTitlebarLayoutNames(),
        state.config.display.layout,
        nativeTitlebarLayoutItems_,
        nativeTitlebarSelectedLayout_);
    PopulateNativeTitlebarCombo(titlebarThemeComboHwnd_,
        NativeTitlebarThemeNames(),
        TitlebarThemeDisplayName(state.config.display.theme),
        nativeTitlebarThemeItems_,
        nativeTitlebarSelectedTheme_);
}

void DashboardApp::UpdateNativeTitlebarControls() {
    if (!nativeTitlebarVisible_ || titlebarHoverProbeHwnd_ == nullptr || titlebarLayoutComboHwnd_ == nullptr ||
        titlebarThemeComboHwnd_ == nullptr) {
        ShowNativeTitlebarControls(false);
        return;
    }

    SyncNativeTitlebarControls();
    nativeTitlebarControlsVisible_ = true;
    if (nativeTitlebarComboDropdownOpen_) {
        return;
    }

    PositionNativeTitlebarCombo(titlebarLayoutComboHwnd_, NativeTitlebarLayoutComboRect());
    PositionNativeTitlebarCombo(titlebarThemeComboHwnd_, NativeTitlebarThemeComboRect());
}

void DashboardApp::ShowNativeTitlebarControls(bool show) {
    nativeTitlebarControlsVisible_ = show;
    if (show) {
        return;
    }
    HideTitlebarTooltip();
    nativeTitlebarComboDropdownOpen_ = false;
    if (titlebarLayoutComboHwnd_ != nullptr && IsWindowVisible(titlebarLayoutComboHwnd_)) {
        ShowWindow(titlebarLayoutComboHwnd_, SW_HIDE);
    }
    if (titlebarThemeComboHwnd_ != nullptr && IsWindowVisible(titlebarThemeComboHwnd_)) {
        ShowWindow(titlebarThemeComboHwnd_, SW_HIDE);
    }
}

int DashboardApp::NativeTitlebarComboClosedHeight(HWND combo) const {
    const int fallbackHeight = ScaleLogicalToPhysical(kTitlebarComboHeightLogical, CurrentWindowDpi());
    if (combo == nullptr) {
        return fallbackHeight;
    }

    const LRESULT selectionHeight = SendMessageA(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
    if (selectionHeight == CB_ERR || selectionHeight <= 0) {
        return fallbackHeight;
    }
    return static_cast<int>(selectionHeight);
}

int DashboardApp::NativeTitlebarComboWindowHeight(HWND combo, const RECT& closedRect) const {
    const int closedHeight = RectHeight(closedRect);
    if (combo == nullptr || closedHeight <= 0) {
        return closedHeight;
    }

    LRESULT itemHeight = SendMessageA(combo, CB_GETITEMHEIGHT, 0, 0);
    if (itemHeight == CB_ERR || itemHeight <= 0) {
        itemHeight = closedHeight;
    }

    const LRESULT itemCountResult = SendMessageA(combo, CB_GETCOUNT, 0, 0);
    const int itemCount = itemCountResult == CB_ERR ? 1 : static_cast<int>(itemCountResult);
    const int visibleRows = std::clamp(itemCount, 1, kTitlebarComboVisibleRows);
    return closedHeight + static_cast<int>(itemHeight) * visibleRows +
           ScaleLogicalToPhysical(kTitlebarComboDropPaddingLogical, CurrentWindowDpi());
}

void DashboardApp::PositionNativeTitlebarCombo(HWND combo, const RECT& closedRect) {
    if (combo == nullptr) {
        return;
    }

    if (!IsRectUsable(closedRect)) {
        if (IsWindowVisible(combo)) {
            ShowWindow(combo, SW_HIDE);
        }
        return;
    }

    RECT targetRect = closedRect;
    targetRect.bottom = targetRect.top + NativeTitlebarComboWindowHeight(combo, closedRect);
    RECT currentRect{};
    bool rectMatches = false;
    if (GetWindowRect(combo, &currentRect)) {
        MapWindowPoints(HWND_DESKTOP, titlebarHoverProbeHwnd_, reinterpret_cast<POINT*>(&currentRect), 2);
        rectMatches = TitlebarRectsEqual(currentRect, targetRect);
    }
    if (!rectMatches) {
        SetWindowPos(combo,
            nullptr,
            targetRect.left,
            targetRect.top,
            RectWidth(targetRect),
            RectHeight(targetRect),
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
    if (!IsWindowVisible(combo)) {
        ShowWindow(combo, SW_SHOWNOACTIVATE);
    }
}

RECT DashboardApp::NativeTitlebarLayoutComboRect() const {
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return {};
    }

    RECT clientRect{};
    GetClientRect(titlebarHoverProbeHwnd_, &clientRect);
    const RECT appMenuRect = NativeTitlebarButtonRect(NativeTitlebarButton::AppMenu);
    const RECT editLayoutRect = NativeTitlebarButtonRect(NativeTitlebarButton::EditLayout);
    const RECT displayRect = NativeTitlebarButtonRect(NativeTitlebarButton::Display);
    const RECT closeRect = NativeTitlebarButtonRect(NativeTitlebarButton::Close);
    const int gap = ScaleLogicalToPhysical(kTitlebarControlGapLogical, CurrentWindowDpi());
    const int right = IsRectUsable(editLayoutRect) ? editLayoutRect.left - gap
                      : IsRectUsable(displayRect)  ? displayRect.left - gap
                                                   : closeRect.left - gap;
    const int minWidth = ScaleLogicalToPhysical(58, CurrentWindowDpi());
    const int desiredWidth = ScaleLogicalToPhysical(kTitlebarLayoutComboWidthLogical, CurrentWindowDpi());
    const LONG leftLimit = IsRectUsable(appMenuRect) ? appMenuRect.right + gap : clientRect.left;
    const int availableWidth = right - leftLimit;
    if (availableWidth < minWidth || RectHeight(clientRect) <= 0) {
        return {};
    }
    const int width = std::min(desiredWidth, availableWidth);
    const int height = std::min(RectHeight(clientRect), NativeTitlebarComboClosedHeight(titlebarLayoutComboHwnd_));
    const int top = clientRect.top + std::max(0, (RectHeight(clientRect) - height) / 2);
    return RECT{right - width, top, right, top + height};
}

RECT DashboardApp::NativeTitlebarThemeComboRect() const {
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return {};
    }

    RECT clientRect{};
    GetClientRect(titlebarHoverProbeHwnd_, &clientRect);
    const RECT appMenuRect = NativeTitlebarButtonRect(NativeTitlebarButton::AppMenu);
    const RECT layoutRect = NativeTitlebarLayoutComboRect();
    const RECT displayRect = NativeTitlebarButtonRect(NativeTitlebarButton::Display);
    const RECT closeRect = NativeTitlebarButtonRect(NativeTitlebarButton::Close);
    const int gap = ScaleLogicalToPhysical(kTitlebarControlGapLogical, CurrentWindowDpi());
    const int right = IsRectUsable(layoutRect)    ? layoutRect.left - gap
                      : IsRectUsable(displayRect) ? displayRect.left - gap
                                                  : closeRect.left - gap;
    const int minWidth = ScaleLogicalToPhysical(76, CurrentWindowDpi());
    const int desiredWidth = ScaleLogicalToPhysical(kTitlebarThemeComboWidthLogical, CurrentWindowDpi());
    const LONG leftLimit = IsRectUsable(appMenuRect) ? appMenuRect.right + gap : clientRect.left;
    const int availableWidth = right - leftLimit;
    if (availableWidth < minWidth || RectHeight(clientRect) <= 0) {
        return {};
    }
    const int width = std::min(desiredWidth, availableWidth);
    const int height = std::min(RectHeight(clientRect), NativeTitlebarComboClosedHeight(titlebarThemeComboHwnd_));
    const int top = clientRect.top + std::max(0, (RectHeight(clientRect) - height) / 2);
    return RECT{right - width, top, right, top + height};
}

RECT DashboardApp::NativeTitlebarButtonRect(NativeTitlebarButton button) const {
    RECT rect{};
    if (titlebarHoverProbeHwnd_ == nullptr) {
        return rect;
    }
    GetClientRect(titlebarHoverProbeHwnd_, &rect);
    const int width = RectWidth(rect);
    const int height = RectHeight(rect);
    if (width <= 0 || height <= 0) {
        return {};
    }

    const int minimumButtonWidth = ScaleLogicalToPhysical(36, CurrentWindowDpi());
    const int closeButtonWidth = std::min(width, std::max(height, minimumButtonWidth));
    if (button == NativeTitlebarButton::AppMenu) {
        rect.right = std::min(rect.right, rect.left + closeButtonWidth);
        return rect;
    }

    if (button == NativeTitlebarButton::Close) {
        rect.left = rect.right - closeButtonWidth;
        return rect;
    }

    if (button == NativeTitlebarButton::Display) {
        const int gap = ScaleLogicalToPhysical(kTitlebarControlGapLogical, CurrentWindowDpi());
        const int right = rect.right - closeButtonWidth - gap;
        const int availableWidth = right - (rect.left + closeButtonWidth + gap);
        if (availableWidth < closeButtonWidth) {
            return {};
        }
        return RECT{right - closeButtonWidth, rect.top, right, rect.bottom};
    }

    if (button == NativeTitlebarButton::EditLayout) {
        const RECT displayRect = NativeTitlebarButtonRect(NativeTitlebarButton::Display);
        if (!IsRectUsable(displayRect)) {
            return {};
        }
        const int gap = ScaleLogicalToPhysical(kTitlebarControlGapLogical, CurrentWindowDpi());
        const int right = displayRect.left - gap;
        const int availableWidth = right - (rect.left + closeButtonWidth + gap);
        if (availableWidth < closeButtonWidth) {
            return {};
        }
        return RECT{right - closeButtonWidth, rect.top, right, rect.bottom};
    }

    return {};
}

void DashboardApp::PopulateNativeTitlebarCombo(HWND combo,
    const std::vector<std::string>& values,
    std::string_view selected,
    std::vector<std::string>& cache,
    std::string& selectedCache) {
    if (combo == nullptr) {
        return;
    }

    if (cache != values) {
        SendMessageA(combo, CB_RESETCONTENT, 0, 0);
        for (const std::string& value : values) {
            SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
        }
        cache = values;
        selectedCache.clear();
    }

    if (selectedCache != selected) {
        int selectedIndex = -1;
        for (size_t i = 0; i < values.size(); ++i) {
            if (values[i] == selected) {
                selectedIndex = static_cast<int>(i);
                break;
            }
        }
        SendMessageA(combo, CB_SETCURSEL, selectedIndex >= 0 ? selectedIndex : -1, 0);
        selectedCache = std::string(selected);
    }
}

std::vector<std::string> DashboardApp::NativeTitlebarLayoutNames() const {
    std::vector<std::string> names;
    const auto& layouts = controller_.State().config.layout.layouts;
    names.reserve(layouts.size());
    for (const LayoutSectionConfig& layout : layouts) {
        names.push_back(layout.name);
    }
    return names;
}

std::vector<std::string> DashboardApp::NativeTitlebarThemeNames() const {
    std::vector<std::string> names;
    const auto& themes = controller_.State().config.layout.themes;
    names.reserve(themes.size());
    for (const ThemeConfig& theme : themes) {
        names.push_back(TitlebarThemeDisplayName(theme.name));
    }
    return names;
}

std::optional<size_t> DashboardApp::NativeTitlebarComboSelectionIndex(HWND combo) const {
    if (combo == nullptr) {
        return std::nullopt;
    }
    const LRESULT selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR || selection < 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(selection);
}

DashboardApp::NativeTitlebarButton DashboardApp::HitTestNativeTitlebarButton(POINT clientPoint) const {
    const RECT closeRect = NativeTitlebarButtonRect(NativeTitlebarButton::Close);
    if (IsRectUsable(closeRect) && PtInRect(&closeRect, clientPoint) != FALSE) {
        return NativeTitlebarButton::Close;
    }

    const RECT displayRect = NativeTitlebarButtonRect(NativeTitlebarButton::Display);
    if (IsRectUsable(displayRect) && PtInRect(&displayRect, clientPoint) != FALSE) {
        return NativeTitlebarButton::Display;
    }

    const RECT editLayoutRect = NativeTitlebarButtonRect(NativeTitlebarButton::EditLayout);
    if (IsRectUsable(editLayoutRect) && PtInRect(&editLayoutRect, clientPoint) != FALSE) {
        return NativeTitlebarButton::EditLayout;
    }

    const RECT appMenuRect = NativeTitlebarButtonRect(NativeTitlebarButton::AppMenu);
    if (IsRectUsable(appMenuRect) && PtInRect(&appMenuRect, clientPoint) != FALSE) {
        return NativeTitlebarButton::AppMenu;
    }

    return NativeTitlebarButton::None;
}

void DashboardApp::PaintNativeTitlebar(HDC hdc) const {
    if (!nativeTitlebarVisible_ || titlebarHoverProbeHwnd_ == nullptr) {
        return;
    }

    RECT clientRect{};
    GetClientRect(titlebarHoverProbeHwnd_, &clientRect);
    if (!IsRectUsable(clientRect)) {
        return;
    }

    FillRectWithColor(hdc, clientRect, nativeTitlebarPalette_.background);
    PaintNativeTitlebarButton(hdc, NativeTitlebarButton::AppMenu);
    PaintNativeTitlebarButton(hdc, NativeTitlebarButton::EditLayout);
    PaintNativeTitlebarButton(hdc, NativeTitlebarButton::Display);
    PaintNativeTitlebarButton(hdc, NativeTitlebarButton::Close);

    const int padding = ScaleLogicalToPhysical(kTitlebarHorizontalPaddingLogical, CurrentWindowDpi());
    const int gap = ScaleLogicalToPhysical(kTitlebarControlGapLogical, CurrentWindowDpi());
    const RECT appMenuRect = NativeTitlebarButtonRect(NativeTitlebarButton::AppMenu);
    const RECT layoutRect = NativeTitlebarLayoutComboRect();
    const RECT themeRect = NativeTitlebarThemeComboRect();
    const RECT editLayoutRect = NativeTitlebarButtonRect(NativeTitlebarButton::EditLayout);
    const RECT displayRect = NativeTitlebarButtonRect(NativeTitlebarButton::Display);
    const RECT closeRect = NativeTitlebarButtonRect(NativeTitlebarButton::Close);
    LONG controlsLeft = closeRect.left;
    if (IsRectUsable(editLayoutRect)) {
        controlsLeft = std::min(controlsLeft, editLayoutRect.left);
    }
    if (IsRectUsable(displayRect)) {
        controlsLeft = std::min(controlsLeft, displayRect.left);
    }
    if (IsRectUsable(themeRect)) {
        controlsLeft = std::min(controlsLeft, themeRect.left);
    }
    if (IsRectUsable(layoutRect)) {
        controlsLeft = std::min(controlsLeft, layoutRect.left);
    }
    const LONG titleLeft = IsRectUsable(appMenuRect) ? appMenuRect.right + gap : clientRect.left + padding;
    RECT textRect{titleLeft, clientRect.top, controlsLeft - padding, clientRect.bottom};
    if (textRect.right > textRect.left) {
        HGDIOBJ oldFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        const int oldBkMode = SetBkMode(hdc, TRANSPARENT);
        const COLORREF oldTextColor = SetTextColor(hdc, nativeTitlebarPalette_.text);
        DrawTextA(hdc, kAppTitle, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(hdc, oldTextColor);
        SetBkMode(hdc, oldBkMode);
        if (oldFont != nullptr) {
            SelectObject(hdc, oldFont);
        }
    }
}

void DashboardApp::PaintNativeTitlebarButton(HDC hdc, NativeTitlebarButton button) const {
    const RECT buttonRect = NativeTitlebarButtonRect(button);
    if (button == NativeTitlebarButton::None || !IsRectUsable(buttonRect)) {
        return;
    }

    const bool hovered = nativeTitlebarHoveredButton_ == button;
    const bool pressed = nativeTitlebarPressedButton_ == button && hovered;
    const bool closeButtonActive = button == NativeTitlebarButton::Close && (hovered || pressed);
    const bool editLayoutActive = button == NativeTitlebarButton::EditLayout && controller_.State().isEditingLayout;
    DashboardCloseButtonColors closeButtonColors{};
    if (closeButtonActive) {
        closeButtonColors = ResolveDashboardCloseButtonColors(titlebarHoverProbeHwnd_, pressed);
        FillRectWithColor(hdc, buttonRect, closeButtonColors.background);
    } else if (pressed) {
        FillRectWithColor(hdc, buttonRect, nativeTitlebarPalette_.buttonPressed);
    } else if (editLayoutActive) {
        FillRectWithColor(hdc, buttonRect, nativeTitlebarPalette_.buttonPressed);
    } else if (hovered) {
        FillRectWithColor(hdc, buttonRect, nativeTitlebarPalette_.buttonHover);
    }

    HGDIOBJ oldFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    const int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    const COLORREF glyphColor = closeButtonActive ? closeButtonColors.glyph
                                : editLayoutActive
                                    ? ColorConfigToColorRef(controller_.State().config.layout.colors.activeEditColor)
                                    : nativeTitlebarPalette_.buttonGlyph;
    const COLORREF oldTextColor = SetTextColor(hdc, glyphColor);

    if (button == NativeTitlebarButton::AppMenu) {
        const int iconSize = NativeTitlebarAppIconSize(CurrentWindowDpi());
        const int iconLeft = (buttonRect.left + buttonRect.right - iconSize) / 2;
        const int iconTop = (buttonRect.top + buttonRect.bottom - iconSize) / 2;
        HICON icon = appIconSmall_ != nullptr ? appIconSmall_ : LoadIconA(nullptr, IDI_APPLICATION);
        if (icon != nullptr) {
            DrawIconEx(hdc, iconLeft, iconTop, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
        }
    } else if (button == NativeTitlebarButton::EditLayout) {
        const int glyphSize = std::max(13, ScaleLogicalToPhysical(15, CurrentWindowDpi()));
        const int handleSize = std::max(3, ScaleLogicalToPhysical(4, CurrentWindowDpi()));
        const int centerX = (buttonRect.left + buttonRect.right) / 2;
        const int centerY = (buttonRect.top + buttonRect.bottom) / 2;
        const int halfGlyph = glyphSize / 2;
        const int halfHandle = handleSize / 2;
        HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi())), glyphColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        MoveToEx(hdc, centerX - halfGlyph, centerY, nullptr);
        LineTo(hdc, centerX + halfGlyph + 1, centerY);
        MoveToEx(hdc, centerX, centerY - halfGlyph, nullptr);
        LineTo(hdc, centerX, centerY + halfGlyph + 1);
        Rectangle(hdc, centerX - halfHandle, centerY - halfHandle, centerX + halfHandle + 1, centerY + halfHandle + 1);
        if (oldBrush != nullptr) {
            SelectObject(hdc, oldBrush);
        }
        if (oldPen != nullptr) {
            SelectObject(hdc, oldPen);
        }
        DeleteObject(pen);
    } else if (button == NativeTitlebarButton::Display) {
        const int screenWidth = std::max(14, ScaleLogicalToPhysical(16, CurrentWindowDpi()));
        const int screenHeight = std::max(9, ScaleLogicalToPhysical(10, CurrentWindowDpi()));
        const int centerX = (buttonRect.left + buttonRect.right) / 2;
        const int centerY = (buttonRect.top + buttonRect.bottom) / 2;
        RECT screenRect{centerX - screenWidth / 2,
            centerY - screenHeight / 2 - ScaleLogicalToPhysical(1, CurrentWindowDpi()),
            centerX + (screenWidth + 1) / 2,
            centerY + (screenHeight + 1) / 2 - ScaleLogicalToPhysical(1, CurrentWindowDpi())};
        HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi())), glyphColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, screenRect.left, screenRect.top, screenRect.right, screenRect.bottom);
        MoveToEx(hdc, centerX, screenRect.bottom, nullptr);
        LineTo(hdc, centerX, screenRect.bottom + ScaleLogicalToPhysical(3, CurrentWindowDpi()));
        MoveToEx(hdc,
            centerX - ScaleLogicalToPhysical(4, CurrentWindowDpi()),
            screenRect.bottom + ScaleLogicalToPhysical(3, CurrentWindowDpi()),
            nullptr);
        LineTo(hdc,
            centerX + ScaleLogicalToPhysical(4, CurrentWindowDpi()) + 1,
            screenRect.bottom + ScaleLogicalToPhysical(3, CurrentWindowDpi()));
        if (oldBrush != nullptr) {
            SelectObject(hdc, oldBrush);
        }
        if (oldPen != nullptr) {
            SelectObject(hdc, oldPen);
        }
        DeleteObject(pen);
    } else if (button == NativeTitlebarButton::Close) {
        const int glyphSize = NativeTitlebarGlyphSize(CurrentWindowDpi());
        const int halfGlyph = glyphSize / 2;
        const int centerX = (buttonRect.left + buttonRect.right) / 2;
        const int centerY = (buttonRect.top + buttonRect.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi())), glyphColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, centerX - halfGlyph, centerY - halfGlyph, nullptr);
        LineTo(hdc, centerX + halfGlyph + 1, centerY + halfGlyph + 1);
        MoveToEx(hdc, centerX + halfGlyph, centerY - halfGlyph, nullptr);
        LineTo(hdc, centerX - halfGlyph - 1, centerY + halfGlyph + 1);
        if (oldPen != nullptr) {
            SelectObject(hdc, oldPen);
        }
        DeleteObject(pen);
    }

    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (oldFont != nullptr) {
        SelectObject(hdc, oldFont);
    }
}

void DashboardApp::InvalidateNativeTitlebar() const {
    if (nativeTitlebarVisible_ && titlebarHoverProbeHwnd_ != nullptr) {
        InvalidateRect(titlebarHoverProbeHwnd_, nullptr, FALSE);
    }
}

void DashboardApp::RefreshNativeTitlebarChrome() {
    if (hwnd_ == nullptr) {
        return;
    }

    nativeTitlebarPalette_ = ResolveDashboardTitlebarPalette(hwnd_);
    const DashboardTitlebarChromeResult chromeResult = ApplyDashboardTitlebarChrome(hwnd_, nativeTitlebarVisible_);
    if (!DashboardTitlebarChromeSucceeded(chromeResult) && trace_.Enabled(TracePrefix::Diagnostics)) {
        trace_.WriteFmt(TracePrefix::Diagnostics,
            "titlebar_chrome corner=0x%08lx border=0x%08lx caption=0x%08lx text=0x%08lx dark=0x%08lx visible=%d",
            static_cast<unsigned long>(chromeResult.cornerPreference),
            static_cast<unsigned long>(chromeResult.borderColor),
            static_cast<unsigned long>(chromeResult.captionColor),
            static_cast<unsigned long>(chromeResult.textColor),
            static_cast<unsigned long>(chromeResult.darkMode),
            nativeTitlebarVisible_ ? 1 : 0);
    }
    InvalidateNativeTitlebar();
}

void DashboardApp::SetNativeTitlebarButtonState(NativeTitlebarButton hovered, NativeTitlebarButton pressed) {
    if (nativeTitlebarHoveredButton_ == hovered && nativeTitlebarPressedButton_ == pressed) {
        return;
    }
    nativeTitlebarHoveredButton_ = hovered;
    nativeTitlebarPressedButton_ = pressed;
    InvalidateNativeTitlebar();
}

void DashboardApp::ResetNativeTitlebarButtonState() {
    SetNativeTitlebarButtonState(NativeTitlebarButton::None, NativeTitlebarButton::None);
}

void DashboardApp::UpdateNativeTitlebarButtonHover(POINT screenPoint) {
    if (!nativeTitlebarVisible_ || titlebarHoverProbeHwnd_ == nullptr) {
        ResetNativeTitlebarButtonState();
        return;
    }
    POINT clientPoint = screenPoint;
    ScreenToClient(titlebarHoverProbeHwnd_, &clientPoint);
    SetNativeTitlebarButtonState(HitTestNativeTitlebarButton(clientPoint), nativeTitlebarPressedButton_);
}

void DashboardApp::UpdateNativeTitlebarTooltip(POINT screenPoint) {
    if (!nativeTitlebarVisible_ || titlebarHoverProbeHwnd_ == nullptr || nativeTitlebarComboDropdownOpen_) {
        HideTitlebarTooltip(nativeTitlebarComboDropdownOpen_ ? "titlebar_combo_open" : "titlebar_unavailable");
        return;
    }

    POINT probeClientPoint = screenPoint;
    ScreenToClient(titlebarHoverProbeHwnd_, &probeClientPoint);
    const DashboardTitlebarTooltipTarget target = ResolveDashboardTitlebarTooltipTarget(probeClientPoint,
        NativeTitlebarButtonRect(NativeTitlebarButton::AppMenu),
        NativeTitlebarLayoutComboRect(),
        NativeTitlebarThemeComboRect(),
        NativeTitlebarButtonRect(NativeTitlebarButton::EditLayout),
        NativeTitlebarButtonRect(NativeTitlebarButton::Display),
        NativeTitlebarButtonRect(NativeTitlebarButton::Close));
    if (target.control == DashboardTitlebarTooltipControl::None || target.localizationKey[0] == '\0') {
        HideTitlebarTooltip("titlebar_no_target");
        return;
    }

    const std::string text = FindLocalizedText(target.localizationKey);
    if (text.empty()) {
        HideTitlebarTooltip("titlebar_empty_text");
        return;
    }

    RECT tooltipRect = target.rect;
    MapWindowPoints(titlebarHoverProbeHwnd_, hwnd_, reinterpret_cast<POINT*>(&tooltipRect), 2);
    if (dashboardTooltipOwner_ == DashboardTooltipOwner::Titlebar && nativeTitlebarTooltipControl_ == target.control &&
        nativeTitlebarTooltipRectValid_ && TitlebarRectsEqual(nativeTitlebarTooltipRect_, tooltipRect)) {
        return;
    }

    POINT tooltipScreenPoint{target.rect.left, target.rect.bottom + ScaleLogicalToPhysical(8, CurrentWindowDpi())};
    ClientToScreen(titlebarHoverProbeHwnd_, &tooltipScreenPoint);
    dashboardTooltip_.ShowOrUpdate(tooltipRect,
        tooltipScreenPoint,
        text,
        ScaleLogicalToPhysical(260, CurrentWindowDpi()),
        "titlebar",
        target.localizationKey);
    dashboardTooltipOwner_ = DashboardTooltipOwner::Titlebar;
    nativeTitlebarTooltipControl_ = target.control;
    nativeTitlebarTooltipRect_ = tooltipRect;
    nativeTitlebarTooltipRectValid_ = true;

    POINT tooltipClientPoint = probeClientPoint;
    MapWindowPoints(titlebarHoverProbeHwnd_, hwnd_, &tooltipClientPoint, 1);
    dashboardTooltip_.RelayMouseMessage(WM_MOUSEMOVE, 0, MAKELPARAM(tooltipClientPoint.x, tooltipClientPoint.y));
}

void DashboardApp::InvokeNativeTitlebarButton(NativeTitlebarButton button) {
    if (button == NativeTitlebarButton::Close) {
        if (hwnd_ != nullptr) {
            PostMessageA(hwnd_, WM_CLOSE, 0, 0);
        }
        return;
    }

    if (button == NativeTitlebarButton::Display && shellUi_ != nullptr && titlebarHoverProbeHwnd_ != nullptr) {
        const RECT displayRect = NativeTitlebarButtonRect(NativeTitlebarButton::Display);
        if (!IsRectUsable(displayRect)) {
            return;
        }
        POINT menuPoint{displayRect.left, displayRect.bottom};
        ClientToScreen(titlebarHoverProbeHwnd_, &menuPoint);
        shellUi_->ShowTitlebarConfigureDisplayMenu(menuPoint);
        UpdateNativeTitlebarHoverFromCursor();
    }

    if (button == NativeTitlebarButton::EditLayout && shellUi_ != nullptr) {
        shellUi_->HandleEditLayoutToggle();
        UpdateLayoutEditTooltip();
        InvalidateNativeTitlebar();
        UpdateNativeTitlebarHoverFromCursor();
    }

    if (button == NativeTitlebarButton::AppMenu && shellUi_ != nullptr && titlebarHoverProbeHwnd_ != nullptr) {
        const RECT appMenuRect = NativeTitlebarButtonRect(NativeTitlebarButton::AppMenu);
        if (!IsRectUsable(appMenuRect)) {
            return;
        }
        POINT menuPoint{appMenuRect.left, appMenuRect.bottom};
        ClientToScreen(titlebarHoverProbeHwnd_, &menuPoint);
        shellUi_->ShowContextMenu(DashboardShellUi::MenuSource::AppWindow, menuPoint, nullptr);
        UpdateNativeTitlebarHoverFromCursor();
    }
}

void DashboardApp::UpdateNativeTitlebarHoverFromCursor() {
    if (hwnd_ == nullptr || !IsWindowVisible(hwnd_) || controller_.State().isMoving || layoutEditModalUiDepth_ > 0) {
        HideTitlebarTooltip("titlebar_unavailable");
        return;
    }

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        HideNativeTitlebar();
        StopNativeTitlebarHoverTimer();
        return;
    }

    const RECT clientRect = DashboardClientScreenRect();
    const DashboardTitlebarGeometry geometry = ResolveNativeTitlebarGeometry(clientRect);
    const bool cursorInClient = PtInRect(&clientRect, cursor) != FALSE;
    const bool cursorInTitlebarBand = geometry.canShow && PtInRect(&geometry.virtualHoverRect, cursor) != FALSE;
    const bool cursorInsideHoverArea = cursorInClient || cursorInTitlebarBand;
    if (nativeTitlebarComboDropdownOpen_) {
        HideTitlebarTooltip("titlebar_combo_open");
        StartNativeTitlebarHoverTimer();
        return;
    }
    if (nativeTitlebarPressedButton_ != NativeTitlebarButton::None) {
        UpdateNativeTitlebarButtonHover(cursor);
        HideTitlebarTooltip("titlebar_pressed");
        StartNativeTitlebarHoverTimer();
        return;
    }

    if (cursorInsideHoverArea) {
        const bool enteredHoverArea = !nativeTitlebarHoverInside_;
        nativeTitlebarHoverInside_ = true;

        // The probe window and child controls are expensive visual state; keep them stable while the pointer stays
        // inside the combined dashboard/titlebar area.
        if (enteredHoverArea && !nativeTitlebarVisible_ && geometry.canShow) {
            ShowNativeTitlebar(geometry);
        } else if (nativeTitlebarVisible_ && !geometry.canShow) {
            HideNativeTitlebar();
            StopNativeTitlebarHoverTimer();
        } else if (enteredHoverArea && geometry.canShow) {
            UpdateNativeTitlebarProbe();
        }

        if (nativeTitlebarVisible_) {
            UpdateNativeTitlebarButtonHover(cursor);
            const bool layoutEditCanOwnTooltip = cursorInClient && controller_.State().isEditingLayout &&
                                                 shellUi_ != nullptr && !shellUi_->IsLayoutEditModalUiActive();
            if (cursorInTitlebarBand) {
                UpdateNativeTitlebarTooltip(cursor);
            } else if (!layoutEditCanOwnTooltip) {
                HideTitlebarTooltip("titlebar_no_target");
            }
            StartNativeTitlebarHoverTimer();
        }
        return;
    }

    if (nativeTitlebarHoverInside_ || nativeTitlebarVisible_) {
        nativeTitlebarHoverInside_ = false;
        HideNativeTitlebar();
    }
    StopNativeTitlebarHoverTimer();
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
    trace_.WriteFmt(TracePrefix::Diagnostics,
        "bring_to_front hwnd=0x%p visible=%d iconic=%d foreground_set=%d",
        reinterpret_cast<void*>(hwnd_),
        IsWindowVisible(hwnd_) != FALSE,
        IsIconic(hwnd_) != FALSE,
        foregroundSet != FALSE);
    UpdateNativeTitlebarProbe();
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

std::optional<int> DashboardApp::EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget& target,
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
    StartMoveMode(false, POINT{}, true, false, false);
}

void DashboardApp::StartMoveModeAt(POINT cursorAnchorClientPoint) {
    StartMoveMode(true, cursorAnchorClientPoint, true, false, false);
}

void DashboardApp::StartMoveMode(bool hasCursorAnchorClientPoint,
    POINT cursorAnchorClientPoint,
    bool clampCursorAnchorClientPoint,
    bool placeOnRelease,
    bool keepNativeTitlebarDuringMove) {
    if (controller_.State().isEditingLayout) {
        layoutEditController_.CancelInteraction();
    }
    HideLayoutEditTooltip();
    moveCursorAnchorClientPoint_ = cursorAnchorClientPoint;
    hasMoveCursorAnchorClientPoint_ = hasCursorAnchorClientPoint;
    clampMoveCursorAnchorClientPoint_ = clampCursorAnchorClientPoint;
    suppressMoveStopOnNextLeftButtonUp_ = false;
    stopMoveModeWhenLeftButtonReleased_ = placeOnRelease;
    nativeTitlebarDragMoveActive_ = keepNativeTitlebarDuringMove && nativeTitlebarVisible_;
    controller_.State().isMoving = true;
    StopNativeTitlebarHoverTimer();
    if (nativeTitlebarDragMoveActive_) {
        UpdateNativeTitlebarProbe();
    } else {
        HideNativeTitlebar();
    }
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    SyncDashboardMoveOverlayState();
    RedrawMoveFrame();
}

void DashboardApp::StartMoveModeFromNativeTitlebar(POINT screenPoint) {
    POINT clientPoint = screenPoint;
    ScreenToClient(hwnd_, &clientPoint);
    StartMoveMode(true, clientPoint, false, true, true);
}

void DashboardApp::StopMoveMode() {
    if (!controller_.State().isMoving) {
        return;
    }
    hasMoveCursorAnchorClientPoint_ = false;
    clampMoveCursorAnchorClientPoint_ = true;
    suppressMoveStopOnNextLeftButtonUp_ = false;
    stopMoveModeWhenLeftButtonReleased_ = false;
    nativeTitlebarDragMoveActive_ = false;
    controller_.State().isMoving = false;
    KillTimer(hwnd_, kMoveTimerId);
    HideLayoutEditTooltip();
    SyncDashboardMoveOverlayState();
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateNativeTitlebarHoverFromCursor();
}

void DashboardApp::UpdateMoveTracking() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }
    if (stopMoveModeWhenLeftButtonReleased_ && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
        StopMoveMode();
        return;
    }

    POINT cursorClientOffset{};
    if (hasMoveCursorAnchorClientPoint_) {
        cursorClientOffset = moveCursorAnchorClientPoint_;
        if (clampMoveCursorAnchorClientPoint_) {
            cursorClientOffset = ClampPointToWindowBounds(cursorClientOffset, WindowWidth(), WindowHeight());
            moveCursorAnchorClientPoint_ = cursorClientOffset;
        }
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
    const RECT targetClientRect{x, y, x + WindowWidth(), y + WindowHeight()};
    const RECT targetWindowRect = ResolveWindowRectForDashboardClientRect(targetClientRect);
    SetWindowPos(hwnd_, HWND_TOP, targetWindowRect.left, targetWindowRect.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    if (nativeTitlebarVisible_) {
        UpdateNativeTitlebarProbe();
    }
    movePlacementInfo_ =
        GetMonitorPlacementForRect(DashboardClientScreenRect(), controller_.State().config.display.scale);
    SyncDashboardMoveOverlayState();
}

void DashboardApp::SyncDashboardMoveOverlayState() {
    auto& overlayState = rendererDashboardOverlayState_.moveOverlay;
    if (!controller_.State().isMoving) {
        overlayState = {};
        return;
    }

    overlayState.visible = true;
    overlayState.placeOnRelease = stopMoveModeWhenLeftButtonReleased_;
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
    return hwnd_ != nullptr
               ? GetMonitorPlacementForRect(DashboardClientScreenRect(), controller_.State().config.display.scale)
               : movePlacementInfo_;
}

void DashboardApp::ShowError(std::string_view message) const {
    ShowAppMessageBox(hwnd_, message, MB_ICONERROR);
}

bool DashboardApp::CreateDashboardTooltip() {
    dashboardTooltip_.SetTrace(&trace_);
    return dashboardTooltip_.Create(hwnd_, instance_, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
}

void DashboardApp::DestroyDashboardTooltip() {
    dashboardTooltip_.Destroy();
    dashboardTooltipOwner_ = DashboardTooltipOwner::None;
    nativeTitlebarTooltipControl_ = DashboardTitlebarTooltipControl::None;
    nativeTitlebarTooltipRect_ = {};
    nativeTitlebarTooltipRectValid_ = false;
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
    std::string trace =
        FormatText("layout=\"%s\" editing=%s moving=%s modal_depth=%d tooltip_visible=%s tooltip_suppressed=%s "
                   "tooltip_rect_valid=%s mouse_tracking=%s drag_active=%s capture=\"%s\"",
            state.config.display.layout.c_str(),
            Trace::BoolText(state.isEditingLayout),
            Trace::BoolText(state.isMoving),
            layoutEditModalUiDepth_,
            Trace::BoolText(dashboardTooltipOwner_ == DashboardTooltipOwner::LayoutEdit && dashboardTooltip_.Visible()),
            Trace::BoolText(layoutEditTooltipRefreshSuppressed_),
            Trace::BoolText(
                dashboardTooltipOwner_ == DashboardTooltipOwner::LayoutEdit && dashboardTooltip_.TargetRectValid()),
            Trace::BoolText(layoutEditMouseTracking_),
            Trace::BoolText(layoutEditController_.HasActiveDrag()),
            captureText);

    LayoutEditController::TooltipTarget target;
    if (const_cast<LayoutEditController&>(layoutEditController_).CurrentTooltipTarget(target)) {
        AppendFormat(trace,
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

void DashboardApp::HideLayoutEditTooltip(std::string_view reason) {
    if (dashboardTooltipOwner_ != DashboardTooltipOwner::LayoutEdit) {
        return;
    }

    dashboardTooltip_.Hide(reason);
    dashboardTooltipOwner_ = DashboardTooltipOwner::None;
}

void DashboardApp::HideTitlebarTooltip(std::string_view reason) {
    if (dashboardTooltipOwner_ != DashboardTooltipOwner::Titlebar) {
        return;
    }
    dashboardTooltip_.Hide(reason);
    dashboardTooltipOwner_ = DashboardTooltipOwner::None;
    nativeTitlebarTooltipControl_ = DashboardTitlebarTooltipControl::None;
    nativeTitlebarTooltipRect_ = {};
    nativeTitlebarTooltipRectValid_ = false;
}

void DashboardApp::HideTooltipForLayoutEditUpdate(std::string_view reason) {
    if (dashboardTooltipOwner_ == DashboardTooltipOwner::Titlebar) {
        HideTitlebarTooltip(reason);
        return;
    }
    HideLayoutEditTooltip(reason);
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
        HideLayoutEditTooltip("layout_edit_suppressed");
        return;
    }
    if (!controller_.State().isEditingLayout || controller_.State().isMoving || shellUi_->IsLayoutEditModalUiActive()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"inactive\"");
        HideLayoutEditTooltip("layout_edit_inactive");
        return;
    }
    if (layoutEditController_.HasActiveDrag()) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"active_drag\"");
        HideLayoutEditTooltip("layout_edit_active_drag");
        return;
    }

    POINT screenPoint{};
    if (GetCursorPos(&screenPoint) && ShouldIgnoreCoveredLayoutEditPointer(screenPoint, true)) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"covered_pointer\"");
        SuspendCoveredLayoutEditHover();
        return;
    }

    LayoutEditController::TooltipTarget target;
    if (!layoutEditController_.CurrentTooltipTarget(target)) {
        TraceLayoutEditUiEvent(TracePrefix::LayoutEditTooltip, "update_skip", "reason=\"no_target\"");
        HideTooltipForLayoutEditUpdate("layout_edit_no_target");
        return;
    }

    const RenderPoint clientPoint = target.clientPoint;
    std::string tooltipError;
    std::string tooltipText;
    if (!BuildLayoutEditTooltipTextForPayload(controller_.State().config, target.payload, tooltipText, &tooltipError)) {
        const char* reasonText = tooltipError.empty() ? "unsupported_target" : tooltipError.c_str();
        TraceLayoutEditUiEventFmt(TracePrefix::LayoutEditTooltip, "update_abort", "reason=\"%s\"", reasonText);
        HideTooltipForLayoutEditUpdate(reasonText);
        return;
    }

    const int tooltipOffsetX = ScaleLogicalToPhysical(28, CurrentWindowDpi());
    const int tooltipOffsetY = ScaleLogicalToPhysical(24, CurrentWindowDpi());
    const int tooltipRadius = ScaleLogicalToPhysical(10, CurrentWindowDpi());
    const RECT tooltipRect = RectFromPoint(clientPoint, tooltipRadius);
    POINT tooltipScreenPoint{clientPoint.x + tooltipOffsetX, clientPoint.y + tooltipOffsetY};
    ClientToScreen(hwnd_, &tooltipScreenPoint);
    const char* targetKind = LayoutEditTooltipPayloadTraceKind(target.payload);
    dashboardTooltip_.ShowOrUpdate(tooltipRect,
        tooltipScreenPoint,
        tooltipText,
        ScaleLogicalToPhysical(360, CurrentWindowDpi()),
        "layout_edit",
        targetKind);
    dashboardTooltipOwner_ = DashboardTooltipOwner::LayoutEdit;
    dashboardTooltip_.RelayMouseMessage(WM_MOUSEMOVE, 0, MAKELPARAM(clientPoint.x, clientPoint.y));
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
    if (dashboardTooltipOwner_ != DashboardTooltipOwner::LayoutEdit || !dashboardTooltip_.TargetRectValid()) {
        return;
    }

    dashboardTooltip_.RelayMouseMessage(message, wParam, lParam);
}

int DashboardApp::Run() {
    if (bringToFrontOnRun_) {
        BringOnTop();
        ScheduleBringToFrontRetries();
    } else {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    UpdateWindow(hwnd_);
    UpdateNativeTitlebarProbe();

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

LRESULT CALLBACK DashboardApp::TitlebarProbeWndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        auto* app = static_cast<DashboardApp*>(create->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DashboardApp::TitlebarProbeWndProcThunk));
        return app->HandleTitlebarProbeMessage(hwnd, message, wParam, lParam);
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK DashboardApp::TitlebarProbeWndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<DashboardApp*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    return app != nullptr ? app->HandleTitlebarProbeMessage(hwnd, message, wParam, lParam)
                          : DefWindowProcA(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleTitlebarProbeMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            const POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (shellUi_ != nullptr && shellUi_->ShouldDashboardIgnoreMouse(screenPoint)) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_COMMAND: {
            const HWND commandHwnd = reinterpret_cast<HWND>(lParam);
            const int notification = HIWORD(wParam);
            if (commandHwnd == titlebarLayoutComboHwnd_ || commandHwnd == titlebarThemeComboHwnd_) {
                if (notification == CBN_DROPDOWN) {
                    nativeTitlebarComboDropdownOpen_ = true;
                    HideTitlebarTooltip();
                    StartNativeTitlebarHoverTimer();
                    return 0;
                }
                if (notification == CBN_CLOSEUP) {
                    nativeTitlebarComboDropdownOpen_ = false;
                    UpdateNativeTitlebarHoverFromCursor();
                    return 0;
                }
                if (notification == CBN_SELCHANGE && shellUi_ != nullptr) {
                    const std::optional<size_t> selectedIndex = NativeTitlebarComboSelectionIndex(commandHwnd);
                    if (selectedIndex.has_value()) {
                        if (commandHwnd == titlebarLayoutComboHwnd_) {
                            shellUi_->ApplyTitlebarLayoutSelection(*selectedIndex);
                        } else {
                            shellUi_->ApplyTitlebarThemeSelection(*selectedIndex);
                        }
                        SyncNativeTitlebarControls();
                        UpdateNativeTitlebarControls();
                        if (titlebarHoverProbeHwnd_ != nullptr) {
                            InvalidateRect(titlebarHoverProbeHwnd_, nullptr, FALSE);
                        }
                    }
                    return 0;
                }
            }
            break;
        }
        case WM_MOUSEMOVE: {
            POINT screenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &screenPoint);
            if (shellUi_ != nullptr && shellUi_->ShouldDashboardIgnoreMouse(screenPoint)) {
                return 0;
            }
            UpdateNativeTitlebarHoverFromCursor();
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT probeScreenPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd, &probeScreenPoint);
            if (shellUi_ != nullptr && shellUi_->ShouldDashboardIgnoreMouse(probeScreenPoint)) {
                return 0;
            }
            UpdateNativeTitlebarHoverFromCursor();
            if (nativeTitlebarVisible_) {
                const POINT probeClientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const NativeTitlebarButton button = HitTestNativeTitlebarButton(probeClientPoint);
                if (button != NativeTitlebarButton::None) {
                    SetNativeTitlebarButtonState(button, button);
                    SetCapture(hwnd);
                    return 0;
                }
                StartMoveModeFromNativeTitlebar(probeScreenPoint);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (nativeTitlebarPressedButton_ != NativeTitlebarButton::None) {
                const POINT probeClientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                const NativeTitlebarButton pressedButton = nativeTitlebarPressedButton_;
                const bool clicked = HitTestNativeTitlebarButton(probeClientPoint) == pressedButton;
                ResetNativeTitlebarButtonState();
                if (GetCapture() == hwnd) {
                    ReleaseCapture();
                }
                if (clicked) {
                    InvokeNativeTitlebarButton(pressedButton);
                }
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (nativeTitlebarPressedButton_ != NativeTitlebarButton::None) {
                ResetNativeTitlebarButtonState();
            }
            break;
        case WM_SETCURSOR:
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));
            return TRUE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintNativeTitlebar(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            if (hwnd == titlebarHoverProbeHwnd_) {
                titlebarHoverProbeHwnd_ = nullptr;
                nativeTitlebarProbeVisible_ = false;
            }
            break;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    DashboardSessionState& state = controller_.State();
    switch (message) {
        case WM_CREATE:
            currentDpi_ = CurrentWindowDpi();
            RefreshNativeTitlebarChrome();
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
            if (wParam == kTitlebarHoverTimerId) {
                UpdateNativeTitlebarHoverFromCursor();
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
            RefreshNativeTitlebarChrome();
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
        case WM_NCMOUSEMOVE:
            UpdateNativeTitlebarHoverFromCursor();
            break;
        case WM_NCLBUTTONDOWN:
            if (wParam == HTCAPTION && nativeTitlebarVisible_ && shellUi_ != nullptr &&
                !shellUi_->IsLayoutEditModalUiActive()) {
                StartMoveModeFromNativeTitlebar(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                return 0;
            }
            break;
        case WM_NCLBUTTONDBLCLK:
            if (wParam == HTCAPTION && nativeTitlebarVisible_) {
                return 0;
            }
            break;
        case WM_SYSCOMMAND:
            switch (wParam & 0xFFF0) {
                case SC_MOVE:
                    StartMoveMode();
                    return 0;
                case SC_SIZE:
                case SC_MINIMIZE:
                case SC_MAXIMIZE:
                    return 0;
                default:
                    break;
            }
            break;
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            LayoutEditController::TooltipTarget layoutEditTarget;
            const LayoutEditController::TooltipTarget* layoutEditTargetPtr = nullptr;
            if (point.x == -1 && point.y == -1) {
                const RECT rect = DashboardClientScreenRect();
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
            UpdateNativeTitlebarHoverFromCursor();
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
            UpdateNativeTitlebarHoverFromCursor();
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
            UpdateNativeTitlebarHoverFromCursor();
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
                TraceLayoutEditUiEventFmt(TracePrefix::LayoutEditUi,
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
            RefreshNativeTitlebarChrome();
            dashboardTooltip_.SetMaxTipWidth(ScaleLogicalToPhysical(360, CurrentWindowDpi()));
            UpdateLayoutEditTooltip();
            if (state.isMoving) {
                UpdateMoveTracking();
            } else {
                movePlacementInfo_ =
                    GetMonitorPlacementForRect(DashboardClientScreenRect(), controller_.State().config.display.scale);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_DISPLAYCHANGE:
            if (!HandleRenderEnvironmentChange("display_change")) {
                return -1;
            }
            return 0;
        case WM_THEMECHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            RefreshNativeTitlebarChrome();
            if (nativeTitlebarVisible_) {
                UpdateNativeTitlebarControls();
            }
            return 0;
        case WM_DEVICECHANGE:
        case WM_SETTINGCHANGE:
            RefreshNativeTitlebarChrome();
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
            KillTimer(hwnd_, kTitlebarHoverTimerId);
            nativeTitlebarHoverTimerActive_ = false;
            if (state.telemetry != nullptr) {
                state.telemetry->Shutdown();
            }
            UnregisterSessionNotifications();
            DestroyDashboardTooltip();
            DestroyNativeTitlebarProbe();
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
