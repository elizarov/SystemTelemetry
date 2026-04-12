#include "dashboard_app.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "layout_edit_tooltip.h"
#include "localization_catalog.h"
namespace {

constexpr UINT kTooltipToolInfoSize = TTTOOLINFOW_V2_SIZE;
constexpr UINT kLayoutEditTooltipFlags = TTF_SUBCLASS | TTF_TRANSPARENT;

RECT RectFromPoint(POINT point, int radius) {
    return RECT{point.x - radius, point.y - radius, point.x + radius + 1, point.y + radius + 1};
}

constexpr double kPredefinedDisplayScales[] = {1.0, 1.5, 2.0, 2.5, 3.0};
constexpr double kScaleEpsilon = 0.0001;

bool AreScalesEqual(double left, double right) {
    return std::abs(left - right) < kScaleEpsilon;
}

bool IsPredefinedDisplayScale(double scale) {
    for (double predefinedScale : kPredefinedDisplayScales) {
        if (AreScalesEqual(scale, predefinedScale)) {
            return true;
        }
    }
    return false;
}

std::wstring FormatScaleLabel(double scale) {
    std::ostringstream stream;
    stream.precision(12);
    stream << (scale * 100.0);
    std::string value = stream.str();
    if (const size_t dot = value.find('.'); dot != std::string::npos) {
        while (!value.empty() && value.back() == '0') {
            value.pop_back();
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    return WideFromUtf8(value + "%");
}

std::wstring FormatScalePercentageValue(double scale) {
    std::ostringstream stream;
    stream.precision(12);
    stream << (scale * 100.0);
    std::string value = stream.str();
    if (const size_t dot = value.find('.'); dot != std::string::npos) {
        while (!value.empty() && value.back() == '0') {
            value.pop_back();
        }
        if (!value.empty() && value.back() == '.') {
            value.pop_back();
        }
    }
    return WideFromUtf8(value);
}

std::wstring FormatLayoutMenuLabel(const LayoutMenuOption& option) {
    std::wstring label = WideFromUtf8(option.name);
    if (!option.description.empty()) {
        label += L" - ";
        label += WideFromUtf8(option.description);
    }
    return label;
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

struct CustomScaleDialogState {
    double initialScale = 1.0;
    std::optional<double> result;
};

INT_PTR CALLBACK CustomScaleDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CustomScaleDialogState*>(GetWindowLongPtrW(hwnd, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<CustomScaleDialogState*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            const std::wstring initialText = FormatScalePercentageValue(state->initialScale);
            SetDlgItemTextW(hwnd, IDC_CUSTOM_SCALE_EDIT, initialText.c_str());
            SendDlgItemMessageW(hwnd, IDC_CUSTOM_SCALE_EDIT, EM_SETSEL, 0, -1);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    wchar_t buffer[64] = {};
                    GetDlgItemTextW(hwnd, IDC_CUSTOM_SCALE_EDIT, buffer, ARRAYSIZE(buffer));
                    const std::optional<double> percentage = TryParseScaleValue(buffer);
                    if (!percentage.has_value()) {
                        MessageBoxW(hwnd, L"Enter a positive percentage scale.", L"System Telemetry", MB_ICONERROR);
                        SetFocus(GetDlgItem(hwnd, IDC_CUSTOM_SCALE_EDIT));
                        SendDlgItemMessageW(hwnd, IDC_CUSTOM_SCALE_EDIT, EM_SETSEL, 0, -1);
                        return TRUE;
                    }
                    state->result = *percentage / 100.0;
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

}  // namespace

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions)
    : diagnosticsOptions_(diagnosticsOptions), layoutEditController_(*this) {}

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    controller_.State().config = config;
    renderer_.SetConfig(config);
    rendererEditOverlayState_.showLayoutEditGuides =
        controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
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
    return static_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
}

bool DashboardApp::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    renderer_.SetConfig(controller_.State().config);
    rendererEditOverlayState_.showLayoutEditGuides =
        controller_.State().isEditingLayout || diagnosticsOptions_.editLayout;
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
    return controller_.ApplyLayoutGuideWeights(*this, target, weights);
}

bool DashboardApp::ApplyLayoutEditValue(const LayoutEditHost::ValueTarget& target, double value) {
    return controller_.ApplyLayoutEditValue(*this, target, value);
}

std::optional<int> DashboardApp::EvaluateLayoutWidgetExtentForWeights(const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const DashboardRenderer::LayoutWidgetIdentity& widget,
    DashboardRenderer::LayoutGuideAxis axis) {
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
    HideLayoutEditTooltip();
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
    HideLayoutEditTooltip();
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
        cursorOffset = std::max(cursorOffset,
            MeasureFontHeight(hdc, renderer_.SmallFont()) + ScaleLogicalToPhysical(8, CurrentWindowDpi()));
        ReleaseDC(hwnd_, hdc);
    }

    const int x = cursor.x - (WindowWidth() / 2);
    const int y = cursor.y - cursorOffset;
    SetWindowPos(hwnd_, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_, controller_.State().config.display.scale);
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

void DashboardApp::ShowContextMenu(POINT screenPoint) {
    HideLayoutEditTooltip();
    DashboardSessionState& state = controller_.State();
    HMENU menu = CreatePopupMenu();
    HMENU diagnosticsMenu = CreatePopupMenu();
    HMENU layoutMenu = CreatePopupMenu();
    HMENU networkMenu = CreatePopupMenu();
    HMENU scaleMenu = CreatePopupMenu();
    HMENU storageDrivesMenu = CreatePopupMenu();
    HMENU configureDisplayMenu = CreatePopupMenu();
    const UINT autoStartFlags = MF_STRING | (controller_.IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    state.layoutMenuOptions.clear();
    for (size_t i = 0; i < state.config.layouts.size() && (kCommandLayoutBase + i) <= kCommandLayoutMax; ++i) {
        LayoutMenuOption option;
        option.commandId = kCommandLayoutBase + static_cast<UINT>(i);
        option.name = state.config.layouts[i].name;
        option.description = state.config.layouts[i].description;
        state.layoutMenuOptions.push_back(option);
    }
    if (state.layoutMenuOptions.empty()) {
        AppendMenuW(layoutMenu, MF_STRING | MF_GRAYED, kCommandLayoutBase, L"No layouts found");
    } else {
        for (const auto& option : state.layoutMenuOptions) {
            const std::wstring label = FormatLayoutMenuLabel(option);
            const UINT flags = MF_STRING | (state.config.display.layout == option.name ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(layoutMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(layoutMenu, option.commandId);
        }
    }
    state.networkMenuOptions.clear();
    const auto& networkCandidates = state.telemetry->NetworkAdapterCandidates();
    for (size_t i = 0; i < networkCandidates.size() && (kCommandNetworkAdapterBase + i) <= kCommandNetworkAdapterMax;
        ++i) {
        NetworkMenuOption option;
        option.commandId = kCommandNetworkAdapterBase + static_cast<UINT>(i);
        option.adapterName = networkCandidates[i].adapterName;
        option.ipAddress = networkCandidates[i].ipAddress;
        option.selected = networkCandidates[i].selected;
        state.networkMenuOptions.push_back(std::move(option));
    }
    if (state.networkMenuOptions.empty()) {
        AppendMenuW(networkMenu, MF_STRING | MF_GRAYED, kCommandNetworkAdapterBase, L"No adapters found");
    } else {
        for (const auto& option : state.networkMenuOptions) {
            const std::wstring label = WideFromUtf8(FormatNetworkFooterText(option.adapterName, option.ipAddress));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(networkMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(networkMenu, option.commandId);
        }
    }
    state.storageDriveMenuOptions.clear();
    const auto& storageDriveCandidates = state.telemetry->StorageDriveCandidates();
    for (size_t i = 0; i < storageDriveCandidates.size() && (kCommandStorageDriveBase + i) <= kCommandStorageDriveMax;
        ++i) {
        StorageDriveMenuOption option;
        option.commandId = kCommandStorageDriveBase + static_cast<UINT>(i);
        option.driveLetter = storageDriveCandidates[i].letter;
        option.volumeLabel = storageDriveCandidates[i].volumeLabel;
        option.totalGb = storageDriveCandidates[i].totalGb;
        option.selected = storageDriveCandidates[i].selected;
        state.storageDriveMenuOptions.push_back(std::move(option));
    }
    if (state.storageDriveMenuOptions.empty()) {
        AppendMenuW(storageDrivesMenu, MF_STRING | MF_GRAYED, kCommandStorageDriveBase, L"No drives found");
    } else {
        for (const auto& option : state.storageDriveMenuOptions) {
            const std::wstring label =
                WideFromUtf8(FormatStorageDriveMenuText(option.driveLetter, option.volumeLabel, option.totalGb));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(storageDrivesMenu, flags, option.commandId, label.c_str());
        }
    }
    state.scaleMenuOptions.clear();
    {
        ScaleMenuOption option;
        option.commandId = kCommandScaleBase;
        option.label = "Default";
        option.selected = !HasExplicitDisplayScale(state.config.display.scale);
        option.isDefault = true;
        state.scaleMenuOptions.push_back(option);
    }
    std::vector<double> scaleEntries(std::begin(kPredefinedDisplayScales), std::end(kPredefinedDisplayScales));
    if (HasExplicitDisplayScale(state.config.display.scale) && !IsPredefinedDisplayScale(state.config.display.scale)) {
        scaleEntries.push_back(state.config.display.scale);
    }
    std::sort(scaleEntries.begin(), scaleEntries.end());
    scaleEntries.erase(std::unique(scaleEntries.begin(),
                           scaleEntries.end(),
                           [](double left, double right) { return AreScalesEqual(left, right); }),
        scaleEntries.end());
    for (size_t i = 0; i < scaleEntries.size() && (kCommandScaleBase + 1 + i) <= kCommandScaleMax; ++i) {
        ScaleMenuOption option;
        option.commandId = kCommandScaleBase + 1 + static_cast<UINT>(i);
        option.scale = scaleEntries[i];
        option.label = Utf8FromWide(FormatScaleLabel(option.scale));
        option.selected = HasExplicitDisplayScale(state.config.display.scale) &&
                          AreScalesEqual(state.config.display.scale, option.scale);
        option.isCustomEntry = !IsPredefinedDisplayScale(option.scale);
        state.scaleMenuOptions.push_back(std::move(option));
    }
    for (const auto& option : state.scaleMenuOptions) {
        const std::wstring label = WideFromUtf8(option.label);
        const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(scaleMenu, flags, option.commandId, label.c_str());
        SetMenuItemRadioStyle(scaleMenu, option.commandId);
    }
    AppendMenuW(scaleMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(scaleMenu, MF_STRING, kCommandCustomScale, L"Custom...");
    state.configDisplayOptions = EnumerateDisplayMenuOptions(state.config);
    if (state.configDisplayOptions.empty()) {
        AppendMenuW(configureDisplayMenu, MF_STRING | MF_GRAYED, kCommandConfigureDisplayBase, L"No displays found");
    } else {
        for (const auto& option : state.configDisplayOptions) {
            const std::wstring label = WideFromUtf8(option.displayName);
            const UINT flags = MF_STRING | (option.layoutFits ? MF_ENABLED : MF_GRAYED);
            AppendMenuW(configureDisplayMenu, flags, option.commandId, label.c_str());
        }
    }
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveFullConfigAs, L"Save Full Config To...");
    AppendMenuW(diagnosticsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveDumpAs, L"Save Dump To...");
    AppendMenuW(diagnosticsMenu, MF_STRING, kCommandSaveScreenshotAs, L"Save Screenshot To...");
    AppendMenuW(menu, MF_STRING, kCommandMove, L"Move");
    AppendMenuW(
        menu, MF_STRING | (state.isEditingLayout ? MF_CHECKED : MF_UNCHECKED), kCommandEditLayout, L"Edit layout");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandReloadConfig, L"Reload Config");
    AppendMenuW(menu, MF_STRING, kCommandSaveConfig, L"Save Config");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layoutMenu), L"Layout");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scaleMenu), L"Scale");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(networkMenu), L"Network");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(storageDrivesMenu), L"Storage drives");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(configureDisplayMenu), L"Config To Display");
    AppendMenuW(menu, autoStartFlags, kCommandAutoStart, L"Auto-start on user logon");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(diagnosticsMenu), L"Diagnostics");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
    SetForegroundWindow(hwnd_);
    const UINT selected = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        0,
        hwnd_,
        nullptr);
    DestroyMenu(menu);

    switch (selected) {
        case kCommandMove:
            StartMoveMode();
            break;
        case kCommandEditLayout:
            if (state.isEditingLayout) {
                controller_.StopLayoutEditMode(*this, layoutEditController_, diagnosticsOptions_.editLayout);
            } else {
                controller_.StartLayoutEditMode(*this, layoutEditController_);
            }
            UpdateLayoutEditTooltip();
            break;
        case kCommandBringOnTop:
            BringOnTop();
            break;
        case kCommandReloadConfig:
            if (!controller_.ReloadConfigFromDisk(*this, diagnosticsOptions_, layoutEditController_)) {
                MessageBoxW(hwnd_, L"Failed to reload config.ini.", L"System Telemetry", MB_ICONERROR);
            }
            break;
        case kCommandSaveConfig:
            controller_.UpdateConfigFromCurrentPlacement(*this);
            break;
        case kCommandAutoStart:
            controller_.ToggleAutoStart(*this);
            break;
        case kCommandSaveDumpAs:
            controller_.SaveDumpAs(*this);
            break;
        case kCommandSaveScreenshotAs:
            controller_.SaveScreenshotAs(*this, diagnosticsOptions_);
            break;
        case kCommandSaveFullConfigAs:
            controller_.SaveFullConfigAs(*this);
            break;
        case kCommandCustomScale:
            if (const auto scale = PromptCustomScale(); scale.has_value()) {
                controller_.SetDisplayScale(*this, *scale);
            }
            break;
        case kCommandExit:
            DestroyWindow(hwnd_);
            break;
        default:
            if (selected >= kCommandLayoutBase && selected <= kCommandLayoutMax) {
                const auto it = std::find_if(state.layoutMenuOptions.begin(),
                    state.layoutMenuOptions.end(),
                    [selected](const LayoutMenuOption& option) { return option.commandId == selected; });
                if (it != state.layoutMenuOptions.end() &&
                    !controller_.SwitchLayout(*this, it->name, layoutEditController_, diagnosticsOptions_.editLayout)) {
                    MessageBoxW(hwnd_, L"Failed to switch layout.", L"System Telemetry", MB_ICONERROR);
                }
                break;
            }
            if (selected >= kCommandNetworkAdapterBase && selected <= kCommandNetworkAdapterMax) {
                const auto it = std::find_if(state.networkMenuOptions.begin(),
                    state.networkMenuOptions.end(),
                    [selected](const NetworkMenuOption& option) { return option.commandId == selected; });
                if (it != state.networkMenuOptions.end()) {
                    controller_.SelectNetworkAdapter(*this, *it);
                }
                break;
            }
            if (selected >= kCommandStorageDriveBase && selected <= kCommandStorageDriveMax) {
                const auto it = std::find_if(state.storageDriveMenuOptions.begin(),
                    state.storageDriveMenuOptions.end(),
                    [selected](const StorageDriveMenuOption& option) { return option.commandId == selected; });
                if (it != state.storageDriveMenuOptions.end()) {
                    controller_.ToggleStorageDrive(*this, *it);
                }
                break;
            }
            if (selected >= kCommandConfigureDisplayBase && selected <= kCommandConfigureDisplayMax) {
                const auto it = std::find_if(state.configDisplayOptions.begin(),
                    state.configDisplayOptions.end(),
                    [selected](const DisplayMenuOption& option) { return option.commandId == selected; });
                if (it != state.configDisplayOptions.end()) {
                    controller_.ConfigureDisplay(*this, *it);
                }
                break;
            }
            if (selected >= kCommandScaleBase && selected <= kCommandScaleMax) {
                const auto it = std::find_if(state.scaleMenuOptions.begin(),
                    state.scaleMenuOptions.end(),
                    [selected](const ScaleMenuOption& option) { return option.commandId == selected; });
                if (it != state.scaleMenuOptions.end()) {
                    controller_.SetDisplayScale(*this, it->isDefault ? 0.0 : it->scale);
                }
            }
            break;
    }
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
    const LRESULT addToolResult = SendMessageW(layoutEditTooltipHwnd_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&toolInfo));
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
    if (layoutEditTooltipHwnd_ == nullptr || !controller_.State().isEditingLayout || controller_.State().isMoving) {
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
    POINT clientPoint = target->clientPoint;
    if (target->kind == LayoutEditController::TooltipTarget::Kind::WidgetGuide) {
        descriptor = FindLayoutEditTooltipDescriptor(target->widgetGuide.parameter);
        value = target->widgetGuide.value;
        if (clientPoint.x == 0 && clientPoint.y == 0) {
            clientPoint = target->widgetGuide.drawEnd;
        }
    } else {
        descriptor = FindLayoutEditTooltipDescriptor(target->editableAnchor.key.parameter);
        value = static_cast<double>(target->editableAnchor.value);
        if (const auto currentFont =
                FindLayoutEditTooltipFontValue(controller_.State().config, target->editableAnchor.key.parameter);
            currentFont.has_value() && *currentFont != nullptr) {
            fontValue = **currentFont;
        }
        if (clientPoint.x == 0 && clientPoint.y == 0) {
            clientPoint.x = target->editableAnchor.anchorRect.left +
                            (std::max<LONG>(0, target->editableAnchor.anchorRect.right - target->editableAnchor.anchorRect.left) /
                                2);
            clientPoint.y = target->editableAnchor.anchorRect.top +
                            (std::max<LONG>(0, target->editableAnchor.anchorRect.bottom - target->editableAnchor.anchorRect.top) /
                                2);
        }
    }

    if (!descriptor.has_value()) {
        HideLayoutEditTooltip();
        return;
    }

    const std::wstring description = WideFromUtf8(FindLocalizedText(descriptor->configKey));
    layoutEditTooltipText_ =
        descriptor->valueFormat == LayoutEditTooltipValueFormat::FontSpec && fontValue.has_value()
            ? BuildTooltipText(*descriptor, *fontValue, description)
            : BuildTooltipText(*descriptor, value, description);

    const int tooltipRadius = ScaleLogicalToPhysical(10, CurrentWindowDpi());
    layoutEditTooltipRect_ = RectFromPoint(clientPoint, tooltipRadius);
    layoutEditTooltipRectValid_ = true;

    TOOLINFOW toolInfo{};
    toolInfo.cbSize = kTooltipToolInfoSize;
    toolInfo.hwnd = hwnd_;
    toolInfo.uFlags = kLayoutEditTooltipFlags;
    toolInfo.uId = 1;
    toolInfo.rect = layoutEditTooltipRect_;
    toolInfo.lpszText = layoutEditTooltipText_.empty() ? const_cast<LPWSTR>(L"")
                                                       : const_cast<LPWSTR>(layoutEditTooltipText_.c_str());
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

std::optional<double> DashboardApp::PromptCustomScale() const {
    CustomScaleDialogState state;
    state.initialScale = HasExplicitDisplayScale(controller_.State().config.display.scale)
                             ? controller_.State().config.display.scale
                             : ResolveCurrentDisplayScale(CurrentWindowDpi());
    if (DialogBoxParamW(instance_,
            MAKEINTRESOURCEW(IDD_CUSTOM_SCALE),
            hwnd_,
            CustomScaleDialogProc,
            reinterpret_cast<LPARAM>(&state)) == IDOK) {
        return state.result;
    }
    return std::nullopt;
}

void DashboardApp::DrawMoveOverlay(HDC hdc) {
    const int margin = ScaleLogicalToPhysical(16, CurrentWindowDpi());
    const int padding = ScaleLogicalToPhysical(12, CurrentWindowDpi());
    const int lineGap = ScaleLogicalToPhysical(6, CurrentWindowDpi());
    const int cornerRadius = ScaleLogicalToPhysical(14, CurrentWindowDpi());
    const int borderWidth = std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi()));

    char positionText[96];
    sprintf_s(
        positionText, "Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);
    char scaleText[96];
    const double scale = ScaleFromDpi(movePlacementInfo_.dpi);
    sprintf_s(scaleText, "Scale: %.0f%% (%.2fx)", scale * 100.0, scale);

    const std::string titleText = "Move Mode";
    const std::string monitorText = "Monitor: " + movePlacementInfo_.monitorName;
    const std::string hintText = "Left-click to place. Copy monitor name, scale, and x/y into config.";

    const int titleHeight = MeasureFontHeight(hdc, renderer_.LabelFont());
    const int bodyHeight = MeasureFontHeight(hdc, renderer_.SmallFont());
    const int minContentWidth = ScaleLogicalToPhysical(220, CurrentWindowDpi());
    const int maxContentWidth = std::max(minContentWidth, WindowWidth() - margin * 2 - padding * 2);
    int preferredContentWidth = minContentWidth;
    preferredContentWidth =
        std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.LabelFont(), titleText).cx));
    preferredContentWidth =
        std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), monitorText).cx));
    preferredContentWidth =
        std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), positionText).cx));
    preferredContentWidth =
        std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), scaleText).cx));
    const int contentWidth = std::min(maxContentWidth, preferredContentWidth);
    const int hintHeight = MeasureWrappedTextHeight(hdc, renderer_.SmallFont(), hintText, contentWidth);
    const int overlayWidth = contentWidth + padding * 2;
    const int overlayHeight = padding * 2 + titleHeight + lineGap + bodyHeight + lineGap + bodyHeight + lineGap +
                              bodyHeight + lineGap + hintHeight;
    RECT overlay{margin, margin, margin + overlayWidth, margin + overlayHeight};

    HBRUSH fill = CreateSolidBrush(BackgroundColor());
    HPEN border = CreatePen(PS_SOLID, borderWidth, AccentColor());
    HGDIOBJ oldBrush = SelectObject(hdc, fill);
    HGDIOBJ oldPen = SelectObject(hdc, border);
    RoundRect(hdc, overlay.left, overlay.top, overlay.right, overlay.bottom, cornerRadius, cornerRadius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(fill);
    DeleteObject(border);

    int y = overlay.top + padding;
    RECT titleRect{overlay.left + padding, y, overlay.right - padding, y + titleHeight};
    y = titleRect.bottom + lineGap;
    RECT monitorRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = monitorRect.bottom + lineGap;
    RECT positionRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = positionRect.bottom + lineGap;
    RECT scaleRect{overlay.left + padding, y, overlay.right - padding, y + bodyHeight};
    y = scaleRect.bottom + lineGap;
    RECT hintRect{overlay.left + padding, y, overlay.right - padding, overlay.bottom - padding};

    DrawTextBlock(
        hdc, titleRect, titleText, renderer_.LabelFont(), AccentColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc,
        monitorRect,
        monitorText,
        renderer_.SmallFont(),
        ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc,
        positionRect,
        positionText,
        renderer_.SmallFont(),
        ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(
        hdc, scaleRect, scaleText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(
        hdc, hintRect, hintText, renderer_.SmallFont(), MutedTextColor(), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
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
            if (point.x == -1 && point.y == -1) {
                RECT rect{};
                GetWindowRect(hwnd_, &rect);
                point.x = rect.left + 24;
                point.y = rect.top + 24;
            }
            if (state.isMoving) {
                StopMoveMode();
            }
            ShowContextMenu(point);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (state.isEditingLayout) {
                POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (layoutEditController_.HandleLButtonDown(hwnd_, clientPoint)) {
                    UpdateLayoutEditTooltip();
                    return 0;
                }
            }
            break;
        case WM_MOUSEMOVE:
            if (state.isEditingLayout) {
                UpdateLayoutEditMouseTracking();
                POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                layoutEditController_.HandleMouseMove(clientPoint);
                UpdateLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_MOUSELEAVE:
            layoutEditMouseTracking_ = false;
            if (state.isEditingLayout) {
                POINT screenPoint{};
                if (GetCursorPos(&screenPoint)) {
                    POINT clientPoint = screenPoint;
                    ScreenToClient(hwnd_, &clientPoint);
                    RECT clientRect{};
                    GetClientRect(hwnd_, &clientRect);
                    if (PtInRect(&clientRect, clientPoint)) {
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
            if (state.isEditingLayout) {
                POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (layoutEditController_.HandleLButtonUp(clientPoint)) {
                    UpdateLayoutEditTooltip();
                    return 0;
                }
            }
            if (state.isMoving) {
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
            if (state.isEditingLayout &&
                layoutEditController_.HandleCaptureChanged(hwnd_, reinterpret_cast<HWND>(lParam))) {
                UpdateLayoutEditTooltip();
                return 0;
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && state.isEditingLayout) {
                layoutEditController_.HandleSetCursor(hwnd_);
                return TRUE;
            }
            break;
        case kTrayMessage:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                POINT point{};
                GetCursorPos(&point);
                ShowContextMenu(point);
                return 0;
            }
            if (lParam == WM_LBUTTONDBLCLK) {
                BringOnTop();
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
                SendMessageW(layoutEditTooltipHwnd_, TTM_SETMAXTIPWIDTH, 0, ScaleLogicalToPhysical(360, CurrentWindowDpi()));
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
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);

    HDC memDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    HBRUSH background = CreateSolidBrush(BackgroundColor());
    FillRect(memDc, &client, background);
    DeleteObject(background);
    SetBkMode(memDc, TRANSPARENT);

    DrawLayout(memDc, controller_.State().telemetry->Snapshot());
    if (controller_.State().isMoving) {
        DrawMoveOverlay(memDc);
    }

    BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    EndPaint(hwnd_, &ps);
}

void DashboardApp::DrawTextBlock(
    HDC hdc, const RECT& rect, const std::string& text, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    RECT copy = rect;
    const std::wstring wideText = WideFromUtf8(text);
    DrawTextW(hdc, wideText.c_str(), -1, &copy, format);
    SelectObject(hdc, oldFont);
}

void DashboardApp::DrawLayout(HDC hdc, const SystemSnapshot& snapshot) {
    renderer_.Draw(hdc, snapshot, rendererEditOverlayState_);
}
