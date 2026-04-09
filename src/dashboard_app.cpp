#include "app_shared.h"
#include "config_writer.h"

#include <cstdio>

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions)
    : diagnosticsOptions_(diagnosticsOptions), layoutEditController_(*this) {}

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    controller_.State().config = config;
    renderer_.SetConfig(config);
    rendererEditOverlayState_.showLayoutEditGuides = controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
    rendererEditOverlayState_.similarityIndicatorMode = GetSimilarityIndicatorMode(diagnosticsOptions_);
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
    if (!controller_.InitializeSession(*this, diagnosticsOptions_)) {
        return false;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &DashboardApp::WndProcSetup;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(BackgroundColor());
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
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        placement.left = monitor->rect.left + ScaleLogicalToPhysical(config.display.position.x, currentDpi_);
        placement.top = monitor->rect.top + ScaleLogicalToPhysical(config.display.position.y, currentDpi_);
    } else {
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        placement.left = 100 + ScaleLogicalToPhysical(config.display.position.x, currentDpi_);
        placement.top = 100 + ScaleLogicalToPhysical(config.display.position.y, currentDpi_);
    }
    placement.right = placement.left + WindowWidth();
    placement.bottom = placement.top + WindowHeight();

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
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
    return hwnd_ != nullptr;
}

void DashboardApp::ApplyConfigPlacement() {
    const AppConfig& config = controller_.State().config;
    UINT targetDpi = hwnd_ != nullptr ? CurrentWindowDpi() : GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    int left = 100 + ScaleLogicalToPhysical(config.display.position.x, targetDpi);
    int top = 100 + ScaleLogicalToPhysical(config.display.position.y, targetDpi);
    bool monitorResolved = config.display.monitorName.empty();
    if (const auto monitor = FindTargetMonitor(config.display.monitorName); monitor.has_value()) {
        monitorResolved = true;
        targetDpi = monitor->dpi;
        left = monitor->rect.left + ScaleLogicalToPhysical(config.display.position.x, targetDpi);
        top = monitor->rect.top + ScaleLogicalToPhysical(config.display.position.y, targetDpi);
    }

    if (!monitorResolved) {
        return;
    }

    const UINT currentDpi = CurrentWindowDpi();
    if (targetDpi != currentDpi) {
        SetWindowPos(hwnd_, HWND_TOP, left, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if ((CurrentWindowDpi() != targetDpi || currentDpi_ != targetDpi) && !ApplyWindowDpi(targetDpi)) {
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
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        StopPlacementWatch();
    }
}

bool DashboardApp::InitializeFonts() {
    renderer_.SetConfig(controller_.State().config);
    renderer_.SetTraceOutput(controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr);
    return renderer_.Initialize(hwnd_);
}

void DashboardApp::ReleaseFonts() {
    renderer_.Shutdown();
}

COLORREF DashboardApp::BackgroundColor() const {
    return ToColorRef(controller_.State().config.layout.colors.backgroundColor);
}

COLORREF DashboardApp::ForegroundColor() const {
    return ToColorRef(controller_.State().config.layout.colors.foregroundColor);
}

COLORREF DashboardApp::AccentColor() const {
    return ToColorRef(controller_.State().config.layout.colors.accentColor);
}

COLORREF DashboardApp::MutedTextColor() const {
    return ToColorRef(controller_.State().config.layout.colors.mutedTextColor);
}

HICON DashboardApp::LoadAppIcon(int width, int height) {
    return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        width, height, LR_DEFAULTCOLOR));
}

bool DashboardApp::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    renderer_.SetConfig(controller_.State().config);
    rendererEditOverlayState_.showLayoutEditGuides = controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
    renderer_.SetTraceOutput(controller_.State().diagnostics != nullptr ? controller_.State().diagnostics->TraceStream() : nullptr);
    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    return renderer_.SaveSnapshotPng(imagePath, snapshot, rendererEditOverlayState_);
}

bool DashboardApp::ApplyWindowDpi(UINT dpi, const RECT* suggestedRect) {
    const UINT targetDpi = std::max(kDefaultDpi, dpi);
    if (currentDpi_ == targetDpi && suggestedRect == nullptr) {
        return true;
    }

    currentDpi_ = targetDpi;
    ReleaseFonts();
    UpdateRendererScale(ScaleFromDpi(currentDpi_));
    if (!InitializeFonts()) {
        return false;
    }

    if (suggestedRect != nullptr) {
        SetWindowPos(hwnd_, nullptr,
            suggestedRect->left,
            suggestedRect->top,
            suggestedRect->right - suggestedRect->left,
            suggestedRect->bottom - suggestedRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

bool DashboardApp::WriteDiagnosticsOutputs() {
    return controller_.WriteDiagnosticsOutputs();
}

std::optional<std::filesystem::path> DashboardApp::PromptDiagnosticsSavePath(
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension) const {
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

bool DashboardApp::ApplyLayoutGuideWeights(const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    return controller_.ApplyLayoutGuideWeights(*this, target, weights);
}

bool DashboardApp::ApplyLayoutEditValue(const LayoutEditHost::ValueTarget& target, double value) {
    return controller_.ApplyLayoutEditValue(*this, target, value);
}

std::optional<int> DashboardApp::EvaluateLayoutWidgetExtentForWeights(const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights, const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) {
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

void DashboardApp::StartMoveMode() {
    if (controller_.State().isEditingLayout) {
        controller_.StopLayoutEditMode(*this, layoutEditController_, diagnosticsOptions_.editLayout);
    }
    controller_.State().isMoving = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StopMoveMode() {
    if (!controller_.State().isMoving) {
        return;
    }
    controller_.State().isMoving = false;
    KillTimer(hwnd_, kMoveTimerId);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::UpdateMoveTracking() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        return;
    }

    HDC hdc = GetDC(hwnd_);
    int cursorOffset = ScaleLogicalToPhysical(24, CurrentWindowDpi());
    if (hdc != nullptr) {
        cursorOffset = std::max(cursorOffset, MeasureFontHeight(hdc, renderer_.SmallFont()) + ScaleLogicalToPhysical(8, CurrentWindowDpi()));
        ReleaseDC(hwnd_, hdc);
    }

    const int x = cursor.x - (WindowWidth() / 2);
    const int y = cursor.y - cursorOffset;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
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
    InvalidateRect(hwnd_, nullptr, FALSE);
}

MonitorPlacementInfo DashboardApp::GetWindowPlacementInfo() const {
    return hwnd_ != nullptr ? GetMonitorPlacementForWindow(hwnd_) : movePlacementInfo_;
}

void DashboardApp::ShowError(const std::wstring& message) const {
    MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
}

