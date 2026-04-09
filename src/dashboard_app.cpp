#include "dashboard_app.h"

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

void DashboardApp::ShowContextMenu(POINT screenPoint) {
    DashboardSessionState& state = controller_.State();
    HMENU menu = CreatePopupMenu();
    HMENU diagnosticsMenu = CreatePopupMenu();
    HMENU layoutMenu = CreatePopupMenu();
    HMENU networkMenu = CreatePopupMenu();
    HMENU storageDrivesMenu = CreatePopupMenu();
    HMENU configureDisplayMenu = CreatePopupMenu();
    const UINT autoStartFlags = MF_STRING | (controller_.IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    state.layoutMenuOptions.clear();
    for (size_t i = 0; i < state.config.layouts.size() && (kCommandLayoutBase + i) <= kCommandLayoutMax; ++i) {
        LayoutMenuOption option;
        option.commandId = kCommandLayoutBase + static_cast<UINT>(i);
        option.name = state.config.layouts[i].name;
        state.layoutMenuOptions.push_back(option);
    }
    if (state.layoutMenuOptions.empty()) {
        AppendMenuW(layoutMenu, MF_STRING | MF_GRAYED, kCommandLayoutBase, L"No layouts found");
    } else {
        for (const auto& option : state.layoutMenuOptions) {
            const std::wstring label = WideFromUtf8(option.name);
            const UINT flags = MF_STRING | (state.config.display.layout == option.name ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(layoutMenu, flags, option.commandId, label.c_str());
            SetMenuItemRadioStyle(layoutMenu, option.commandId);
        }
    }
    state.networkMenuOptions.clear();
    const auto& networkCandidates = state.telemetry->NetworkAdapterCandidates();
    for (size_t i = 0; i < networkCandidates.size() && (kCommandNetworkAdapterBase + i) <= kCommandNetworkAdapterMax; ++i) {
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
    for (size_t i = 0; i < storageDriveCandidates.size() && (kCommandStorageDriveBase + i) <= kCommandStorageDriveMax; ++i) {
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
            const std::wstring label = WideFromUtf8(
                FormatStorageDriveMenuText(option.driveLetter, option.volumeLabel, option.totalGb));
            const UINT flags = MF_STRING | (option.selected ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(storageDrivesMenu, flags, option.commandId, label.c_str());
        }
    }
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
    AppendMenuW(menu, MF_STRING | (state.isEditingLayout ? MF_CHECKED : MF_UNCHECKED), kCommandEditLayout, L"Edit layout");
    AppendMenuW(menu, MF_STRING, kCommandBringOnTop, L"Bring On Top");
    AppendMenuW(menu, MF_STRING, kCommandReloadConfig, L"Reload Config");
    AppendMenuW(menu, MF_STRING, kCommandSaveConfig, L"Save Config");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layoutMenu), L"Layout");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(networkMenu), L"Network");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(storageDrivesMenu), L"Storage drives");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(configureDisplayMenu), L"Config To Display");
    AppendMenuW(menu, autoStartFlags, kCommandAutoStart, L"Auto-start on user logon");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(diagnosticsMenu), L"Diagnostics");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
    SetForegroundWindow(hwnd_);
    const UINT selected = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
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
    case kCommandExit:
        DestroyWindow(hwnd_);
        break;
    default:
        if (selected >= kCommandLayoutBase && selected <= kCommandLayoutMax) {
            const auto it = std::find_if(state.layoutMenuOptions.begin(), state.layoutMenuOptions.end(),
                [selected](const LayoutMenuOption& option) { return option.commandId == selected; });
            if (it != state.layoutMenuOptions.end() &&
                !controller_.SwitchLayout(*this, it->name, layoutEditController_, diagnosticsOptions_.editLayout)) {
                MessageBoxW(hwnd_, L"Failed to switch layout.", L"System Telemetry", MB_ICONERROR);
            }
            break;
        }
        if (selected >= kCommandNetworkAdapterBase && selected <= kCommandNetworkAdapterMax) {
            const auto it = std::find_if(state.networkMenuOptions.begin(), state.networkMenuOptions.end(),
                [selected](const NetworkMenuOption& option) { return option.commandId == selected; });
            if (it != state.networkMenuOptions.end()) {
                controller_.SelectNetworkAdapter(*this, *it);
            }
            break;
        }
        if (selected >= kCommandStorageDriveBase && selected <= kCommandStorageDriveMax) {
            const auto it = std::find_if(state.storageDriveMenuOptions.begin(), state.storageDriveMenuOptions.end(),
                [selected](const StorageDriveMenuOption& option) { return option.commandId == selected; });
            if (it != state.storageDriveMenuOptions.end()) {
                controller_.ToggleStorageDrive(*this, *it);
            }
            break;
        }
        if (selected >= kCommandConfigureDisplayBase && selected <= kCommandConfigureDisplayMax) {
            const auto it = std::find_if(state.configDisplayOptions.begin(), state.configDisplayOptions.end(),
                [selected](const DisplayMenuOption& option) { return option.commandId == selected; });
            if (it != state.configDisplayOptions.end()) {
                controller_.ConfigureDisplay(*this, *it);
            }
        }
        break;
    }
}

void DashboardApp::DrawMoveOverlay(HDC hdc) {
    const int margin = ScaleLogicalToPhysical(16, CurrentWindowDpi());
    const int padding = ScaleLogicalToPhysical(12, CurrentWindowDpi());
    const int lineGap = ScaleLogicalToPhysical(6, CurrentWindowDpi());
    const int cornerRadius = ScaleLogicalToPhysical(14, CurrentWindowDpi());
    const int borderWidth = std::max(1, ScaleLogicalToPhysical(1, CurrentWindowDpi()));

    char positionText[96];
    sprintf_s(positionText, "Pos: x=%ld y=%ld", movePlacementInfo_.relativePosition.x, movePlacementInfo_.relativePosition.y);
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
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.LabelFont(), titleText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), monitorText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), positionText).cx));
    preferredContentWidth = std::max(preferredContentWidth, static_cast<int>(MeasureTextSize(hdc, renderer_.SmallFont(), scaleText).cx));
    const int contentWidth = std::min(maxContentWidth, preferredContentWidth);
    const int hintHeight = MeasureWrappedTextHeight(hdc, renderer_.SmallFont(), hintText, contentWidth);
    const int overlayWidth = contentWidth + padding * 2;
    const int overlayHeight = padding * 2 + titleHeight + lineGap + bodyHeight + lineGap +
        bodyHeight + lineGap + bodyHeight + lineGap + hintHeight;
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

    DrawTextBlock(hdc, titleRect, titleText, renderer_.LabelFont(), AccentColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, monitorRect, monitorText, renderer_.SmallFont(), ForegroundColor(),
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    DrawTextBlock(hdc, positionRect, positionText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, scaleRect, scaleText, renderer_.SmallFont(), ForegroundColor(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextBlock(hdc, hintRect, hintText, renderer_.SmallFont(),
        MutedTextColor(), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
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
    return app != nullptr ? app->HandleMessage(message, wParam, lParam)
                          : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DashboardApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    DashboardSessionState& state = controller_.State();
    switch (message) {
    case WM_CREATE:
        currentDpi_ = CurrentWindowDpi();
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
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
                return 0;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (state.isEditingLayout) {
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            layoutEditController_.HandleMouseMove(clientPoint);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (state.isEditingLayout) {
            POINT clientPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (layoutEditController_.HandleLButtonUp(clientPoint)) {
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
                return 0;
            }
            if (state.isMoving) {
                StopMoveMode();
                return 0;
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (state.isEditingLayout && layoutEditController_.HandleCaptureChanged(hwnd_, reinterpret_cast<HWND>(lParam))) {
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
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DISPLAYCHANGE:
        StartPlacementWatch();
        RetryConfigPlacementIfPending();
        if (!ApplyWindowDpi(CurrentWindowDpi())) {
            return -1;
        }
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DEVICECHANGE:
    case WM_SETTINGCHANGE:
        StartPlacementWatch();
        RetryConfigPlacementIfPending();
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, kRefreshTimerId);
        KillTimer(hwnd_, kMoveTimerId);
        KillTimer(hwnd_, kPlacementTimerId);
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

void DashboardApp::DrawTextBlock(HDC hdc, const RECT& rect, const std::string& text, HFONT font,
    COLORREF color, UINT format) {
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

