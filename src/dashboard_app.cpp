#include "app_shared.h"

#include <cmath>
#include <cstdio>

namespace {

bool WidgetIdentityEquals(const DashboardRenderer::LayoutWidgetIdentity& left,
    const DashboardRenderer::LayoutWidgetIdentity& right) {
    return left.renderCardId == right.renderCardId &&
        left.editCardId == right.editCardId &&
        left.nodePath == right.nodePath;
}

bool EditableTextKeyEquals(const DashboardRenderer::EditableTextKey& left,
    const DashboardRenderer::EditableTextKey& right) {
    return WidgetIdentityEquals(left.widget, right.widget) &&
        left.fontRole == right.fontRole &&
        left.textId == right.textId;
}

}

DashboardApp::DashboardApp(const DiagnosticsOptions& diagnosticsOptions) : diagnosticsOptions_(diagnosticsOptions) {}

void DashboardApp::SetRenderConfig(const AppConfig& config) {
    config_ = config;
    renderer_.SetConfig(config);
    renderer_.SetShowLayoutEditGuides(isEditingLayout_ || diagnosticsOptions_.editLayout);
    renderer_.SetSimilarityIndicatorMode(GetSimilarityIndicatorMode(diagnosticsOptions_));
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
    return isEditingLayout_;
}

int DashboardApp::WindowWidth() const {
    return renderer_.WindowWidth();
}

int DashboardApp::WindowHeight() const {
    return renderer_.WindowHeight();
}

bool DashboardApp::Initialize(HINSTANCE instance) {
    instance_ = instance;
    config_ = LoadRuntimeConfig(diagnosticsOptions_);
    if (!ApplyDiagnosticsLayoutOverride(config_, diagnosticsOptions_)) {
        return false;
    }
    renderer_.SetConfig(config_);
    renderer_.SetShowLayoutEditGuides(diagnosticsOptions_.editLayout);
    renderer_.SetSimilarityIndicatorMode(GetSimilarityIndicatorMode(diagnosticsOptions_));
    renderer_.SetTraceOutput(nullptr);
    telemetry_ = CreateTelemetryRuntime(diagnosticsOptions_, GetWorkingDirectory());
    if (diagnosticsOptions_.HasAnyOutput()) {
        diagnostics_ = std::make_unique<DiagnosticsSession>(diagnosticsOptions_);
        if (!diagnostics_->Initialize()) {
            return false;
        }
        diagnostics_->WriteTraceMarker("diagnostics:ui_start");
        diagnostics_->WriteTraceMarker("diagnostics:telemetry_initialize_begin");
    }
    if (telemetry_ == nullptr || !telemetry_->Initialize(config_, diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr)) {
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:telemetry_initialize_failed");
        }
        return false;
    }
    if (diagnostics_ != nullptr) {
        diagnostics_->WriteTraceMarker("diagnostics:telemetry_initialized");
        renderer_.SetTraceOutput(diagnostics_->TraceStream());
        lastDiagnosticsOutput_ = std::chrono::steady_clock::now();
    }
    if (diagnosticsOptions_.editLayout) {
        isEditingLayout_ = true;
        renderer_.SetShowLayoutEditGuides(true);
    }

    ApplyConfiguredWallpaper();

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

    RECT placement{100, 100, 100 + WindowWidth(), 100 + WindowHeight()};
    currentDpi_ = GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    if (const auto monitor = FindTargetMonitor(config_.display.monitorName); monitor.has_value()) {
        currentDpi_ = monitor->dpi;
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        placement.left = monitor->rect.left + ScaleLogicalToPhysical(config_.display.position.x, currentDpi_);
        placement.top = monitor->rect.top + ScaleLogicalToPhysical(config_.display.position.y, currentDpi_);
    } else {
        UpdateRendererScale(ScaleFromDpi(currentDpi_));
        placement.left = 100 + ScaleLogicalToPhysical(config_.display.position.x, currentDpi_);
        placement.top = 100 + ScaleLogicalToPhysical(config_.display.position.y, currentDpi_);
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
    UINT targetDpi = hwnd_ != nullptr ? CurrentWindowDpi() : GetMonitorDpi(MonitorFromPoint(POINT{100, 100}, MONITOR_DEFAULTTOPRIMARY));
    int left = 100 + ScaleLogicalToPhysical(config_.display.position.x, targetDpi);
    int top = 100 + ScaleLogicalToPhysical(config_.display.position.y, targetDpi);
    bool monitorResolved = config_.display.monitorName.empty();
    if (const auto monitor = FindTargetMonitor(config_.display.monitorName); monitor.has_value()) {
        monitorResolved = true;
        targetDpi = monitor->dpi;
        left = monitor->rect.left + ScaleLogicalToPhysical(config_.display.position.x, targetDpi);
        top = monitor->rect.top + ScaleLogicalToPhysical(config_.display.position.y, targetDpi);
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
    if (hwnd_ == nullptr || config_.display.monitorName.empty()) {
        StopPlacementWatch();
        return;
    }
    SetTimer(hwnd_, kPlacementTimerId, kPlacementTimerMs, nullptr);
    placementWatchActive_ = true;
}

void DashboardApp::StopPlacementWatch() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, kPlacementTimerId);
    }
    placementWatchActive_ = false;
}

void DashboardApp::RetryConfigPlacementIfPending() {
    if (!placementWatchActive_ || hwnd_ == nullptr || isMoving_) {
        return;
    }
    if (config_.display.monitorName.empty() || FindTargetMonitor(config_.display.monitorName).has_value()) {
        ApplyConfigPlacement();
        ApplyConfiguredWallpaper();
        movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        StopPlacementWatch();
    }
}

bool DashboardApp::InitializeFonts() {
    renderer_.SetConfig(config_);
    renderer_.SetTraceOutput(diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr);
    return renderer_.Initialize(hwnd_);
}

void DashboardApp::ReleaseFonts() {
    renderer_.Shutdown();
}

COLORREF DashboardApp::BackgroundColor() const {
    return ToColorRef(config_.layout.colors.backgroundColor);
}

COLORREF DashboardApp::ForegroundColor() const {
    return ToColorRef(config_.layout.colors.foregroundColor);
}

COLORREF DashboardApp::AccentColor() const {
    return ToColorRef(config_.layout.colors.accentColor);
}

COLORREF DashboardApp::MutedTextColor() const {
    return ToColorRef(config_.layout.colors.mutedTextColor);
}

HICON DashboardApp::LoadAppIcon(int width, int height) {
    return static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        width, height, LR_DEFAULTCOLOR));
}

bool DashboardApp::SaveSnapshotPng(const std::filesystem::path& imagePath, const SystemSnapshot& snapshot) {
    renderer_.SetConfig(config_);
    renderer_.SetShowLayoutEditGuides(isEditingLayout_ || diagnosticsOptions_.editLayout);
    renderer_.SetTraceOutput(diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr);
    if (!renderer_.Initialize(hwnd_)) {
        return false;
    }
    return renderer_.SaveSnapshotPng(imagePath, snapshot);
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
    if (diagnostics_ == nullptr) {
        return true;
    }
    diagnostics_->WriteTraceMarker("diagnostics:write_outputs_begin");
    const bool ok = diagnostics_->WriteOutputs(telemetry_->Dump(), telemetry_->EffectiveConfig());
    diagnostics_->WriteTraceMarker(ok ? "diagnostics:write_outputs_done" : "diagnostics:write_outputs_failed");
    return ok;
}

std::optional<std::filesystem::path> DashboardApp::PromptDiagnosticsSavePath(
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension) const {
    return PromptSavePath(hwnd_, GetWorkingDirectory(), defaultFileName, filter, defaultExtension);
}

AppConfig DashboardApp::BuildCurrentConfigForSaving() const {
    AppConfig config = telemetry_->EffectiveConfig();
    const MonitorPlacementInfo placement = GetMonitorPlacementForWindow(hwnd_);
    const std::string monitorName = !placement.configMonitorName.empty()
        ? placement.configMonitorName
        : placement.deviceName;
    config.display.monitorName = monitorName;
    config.display.position.x = placement.relativePosition.x;
    config.display.position.y = placement.relativePosition.y;
    return config;
}

void DashboardApp::SaveDumpAs() {
    const auto path = PromptDiagnosticsSavePath(
        kDefaultDumpFileName,
        L"Telemetry dump (*.txt)\0*.txt\0All files (*.*)\0*.*\0",
        L"txt");
    if (!path.has_value()) {
        return;
    }

    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        const std::wstring message =
            WideFromUtf8("Failed to open dump file:\n" + Utf8FromWide(path->wstring()));
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return;
    }

    if (!WriteTelemetryDump(output, telemetry_->Dump())) {
        const std::wstring message =
            WideFromUtf8("Failed to write dump file:\n" + Utf8FromWide(path->wstring()));
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
    }
}

void DashboardApp::SaveScreenshotAs() {
    const auto path = PromptDiagnosticsSavePath(
        kDefaultScreenshotFileName,
        L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0",
        L"png");
    if (!path.has_value()) {
        return;
    }

    std::string errorText;
    if (!SaveDumpScreenshot(
            *path,
            telemetry_->Dump().snapshot,
            telemetry_->EffectiveConfig(),
            1.0,
            GetDiagnosticsRenderMode(diagnosticsOptions_),
            isEditingLayout_ || diagnosticsOptions_.editLayout,
            GetSimilarityIndicatorMode(diagnosticsOptions_),
            diagnosticsOptions_.editLayoutWidgetName,
            diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr,
            &errorText)) {
        std::string message = "Failed to save screenshot:\n" + Utf8FromWide(path->wstring());
        if (!errorText.empty()) {
            message += "\n\n" + errorText;
        }
        const std::wstring wideMessage = WideFromUtf8(message);
        MessageBoxW(hwnd_, wideMessage.c_str(), L"System Telemetry", MB_ICONERROR);
    }
}

void DashboardApp::SaveFullConfigAs() {
    const auto path = PromptDiagnosticsSavePath(
        kDefaultSavedFullConfigFileName,
        L"INI config (*.ini)\0*.ini\0All files (*.*)\0*.*\0",
        L"ini");
    if (!path.has_value()) {
        return;
    }

    if (!SaveFullConfig(*path, BuildCurrentConfigForSaving())) {
        const std::wstring message =
            WideFromUtf8("Failed to save full config file:\n" + Utf8FromWide(path->wstring()));
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
    }
}

bool DashboardApp::IsAutoStartEnabled() const {
    return IsAutoStartEnabledForCurrentExecutable();
}

void DashboardApp::ToggleAutoStart() {
    const bool enable = !IsAutoStartEnabled();
    if (!UpdateAutoStartRegistration(enable, hwnd_)) {
        const wchar_t* action = enable ? L"enable" : L"disable";
        std::wstring message = L"Failed to ";
        message += action;
        message += L" auto-start on user logon.";
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
    }
}

void DashboardApp::BringOnTop() {
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd_);
}

bool DashboardApp::ReloadConfigFromDisk() {
    StopLayoutEditMode();
    if (!ReloadTelemetryRuntimeFromDisk(GetRuntimeConfigPath(), config_, telemetry_, diagnosticsOptions_, diagnostics_.get())) {
        ReleaseFonts();
        InitializeFonts();
        return false;
    }
    ReleaseFonts();
    if (!InitializeFonts()) {
        if (diagnostics_ != nullptr) {
            diagnostics_->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    ApplyConfiguredWallpaper();
    StartPlacementWatch();
    ApplyConfigPlacement();
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool DashboardApp::ApplyConfiguredWallpaper() {
    return ::ApplyConfiguredWallpaper(config_, diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr);
}

bool DashboardApp::ConfigureDisplay(const DisplayMenuOption& option) {
    StopLayoutEditMode();
    if (!option.layoutFits) {
        return false;
    }

    AppConfig updatedConfig = telemetry_->EffectiveConfig();
    updatedConfig.display.monitorName = option.configMonitorName;
    updatedConfig.display.position = {};
    updatedConfig.display.wallpaper = Utf8FromWide(kDefaultBlankWallpaperFileName);

    const std::filesystem::path configPath = GetRuntimeConfigPath();
    const std::filesystem::path imagePath = GetExecutableDirectory() / kDefaultBlankWallpaperFileName;
    const TelemetryDump dump = telemetry_->Dump();

    bool saved = false;
    if (CanWriteRuntimeConfig(configPath) && CanWriteRuntimeConfig(imagePath)) {
        std::string screenshotError;
        saved = SaveDumpScreenshot(
            imagePath,
            dump.snapshot,
            updatedConfig,
            ScaleFromDpi(option.dpi),
            DashboardRenderer::RenderMode::Blank,
            false,
            DashboardRenderer::SimilarityIndicatorMode::ActiveGuide,
            std::string{},
            diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr,
            &screenshotError);
        if (saved) {
            saved = SaveConfig(configPath, updatedConfig);
        }
        if (saved) {
            saved = ::ApplyConfiguredWallpaper(updatedConfig, diagnostics_ != nullptr ? diagnostics_->TraceStream() : nullptr);
        }
    } else {
        const std::filesystem::path tempConfigPath = CreateTempFilePath(L"stc");
        const std::filesystem::path tempDumpPath = CreateTempFilePath(L"std");
        if (tempConfigPath.empty() || tempDumpPath.empty()) {
            saved = false;
        } else {
            saved = SaveConfig(tempConfigPath, updatedConfig);
            if (saved) {
                std::ofstream dumpStream(tempDumpPath, std::ios::binary | std::ios::trunc);
                saved = dumpStream.is_open() && WriteTelemetryDump(dumpStream, dump);
            }
            if (saved) {
                std::wstring parameters = L"/configure-display \"";
                parameters += tempConfigPath.wstring();
                parameters += L"\" /configure-display-target \"";
                parameters += configPath.wstring();
                parameters += L"\" /configure-display-dump \"";
                parameters += tempDumpPath.wstring();
                parameters += L"\" /configure-display-image-target \"";
                parameters += imagePath.wstring();
                parameters += L"\"";

                SHELLEXECUTEINFOW executeInfo{};
                executeInfo.cbSize = sizeof(executeInfo);
                executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
                executeInfo.hwnd = hwnd_;
                executeInfo.lpVerb = L"runas";
                wchar_t modulePath[MAX_PATH];
                const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
                if (length != 0 && length < ARRAYSIZE(modulePath)) {
                    executeInfo.lpFile = modulePath;
                    executeInfo.lpParameters = parameters.c_str();
                    executeInfo.nShow = SW_HIDE;
                    saved = ShellExecuteExW(&executeInfo) == TRUE;
                    if (saved) {
                        WaitForSingleObject(executeInfo.hProcess, INFINITE);
                        DWORD exitCode = 1;
                        GetExitCodeProcess(executeInfo.hProcess, &exitCode);
                        CloseHandle(executeInfo.hProcess);
                        saved = exitCode == 0;
                    }
                } else {
                    saved = false;
                }
            }

            std::error_code ignored;
            std::filesystem::remove(tempConfigPath, ignored);
            std::filesystem::remove(tempDumpPath, ignored);
        }
    }

    if (!saved) {
        MessageBoxW(hwnd_, L"Failed to configure the selected display.", L"System Telemetry", MB_ICONERROR);
        return false;
    }

    config_ = updatedConfig;
    telemetry_->SetEffectiveConfig(config_);
    renderer_.SetConfig(config_);
    ApplyConfiguredWallpaper();
    StartPlacementWatch();
    ApplyConfigPlacement();
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool DashboardApp::SwitchLayout(const std::string& layoutName) {
    StopLayoutEditMode();
    AppConfig updatedConfig = config_;
    if (!SelectLayout(updatedConfig, layoutName)) {
        return false;
    }

    const AppConfig previousConfig = config_;
    ReleaseFonts();
    config_ = updatedConfig;
    telemetry_->SetEffectiveConfig(config_);
    if (!InitializeFonts()) {
        config_ = previousConfig;
        telemetry_->SetEffectiveConfig(config_);
        InitializeFonts();
        return false;
    }

    StartPlacementWatch();
    ApplyConfigPlacement();
    movePlacementInfo_ = GetMonitorPlacementForWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void DashboardApp::SelectNetworkAdapter(const NetworkMenuOption& option) {
    config_.network.adapterName = option.adapterName;
    telemetry_->SetPreferredNetworkAdapterName(option.adapterName);
    telemetry_->UpdateSnapshot();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::ToggleStorageDrive(const StorageDriveMenuOption& option) {
    std::vector<std::string> driveLetters = config_.storage.drives;
    const auto it = std::find(driveLetters.begin(), driveLetters.end(), option.driveLetter);
    if (it == driveLetters.end()) {
        driveLetters.push_back(option.driveLetter);
    } else {
        driveLetters.erase(it);
    }
    std::sort(driveLetters.begin(), driveLetters.end());
    config_.storage.drives = driveLetters;
    renderer_.SetConfig(config_);
    telemetry_->SetSelectedStorageDrives(driveLetters);
    telemetry_->UpdateSnapshot();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StartLayoutEditMode() {
    if (isEditingLayout_) {
        return;
    }
    if (isMoving_) {
        StopMoveMode();
    }
    isEditingLayout_ = true;
    renderer_.SetShowLayoutEditGuides(true);
    renderer_.SetActiveLayoutEditGuide(std::nullopt);
    renderer_.SetHoveredEditableWidget(std::nullopt);
    renderer_.SetActiveWidgetEditGuide(std::nullopt);
    renderer_.SetHoveredEditableText(std::nullopt);
    renderer_.SetActiveEditableText(std::nullopt);
    hoveredLayoutGuideIndex_.reset();
    hoveredEditableWidget_.reset();
    hoveredWidgetEditGuideIndex_.reset();
    hoveredEditableText_.reset();
    hoveredEditableTextAnchor_.reset();
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeTextEditDrag_.reset();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StopLayoutEditMode() {
    if (!isEditingLayout_) {
        return;
    }
    isEditingLayout_ = false;
    renderer_.SetShowLayoutEditGuides(diagnosticsOptions_.editLayout);
    renderer_.SetActiveLayoutEditGuide(std::nullopt);
    renderer_.SetHoveredEditableWidget(std::nullopt);
    renderer_.SetActiveWidgetEditGuide(std::nullopt);
    renderer_.SetHoveredEditableText(std::nullopt);
    renderer_.SetActiveEditableText(std::nullopt);
    hoveredLayoutGuideIndex_.reset();
    hoveredEditableWidget_.reset();
    hoveredWidgetEditGuideIndex_.reset();
    hoveredEditableText_.reset();
    hoveredEditableTextAnchor_.reset();
    activeLayoutDrag_.reset();
    activeWidgetEditDrag_.reset();
    activeTextEditDrag_.reset();
    ReleaseCapture();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

const DashboardRenderer::LayoutEditGuide* DashboardApp::HitTestLayoutGuide(POINT clientPoint, size_t* index) const {
    const auto& guides = renderer_.LayoutEditGuides();
    for (size_t i = 0; i < guides.size(); ++i) {
        if (PtInRect(&guides[i].hitRect, clientPoint)) {
            if (index != nullptr) {
                *index = i;
            }
            return &guides[i];
        }
    }
    return nullptr;
}

const DashboardRenderer::WidgetEditGuide* DashboardApp::HitTestWidgetEditGuide(POINT clientPoint, size_t* index) const {
    const auto& guides = renderer_.WidgetEditGuides();
    for (size_t i = 0; i < guides.size(); ++i) {
        if (PtInRect(&guides[i].hitRect, clientPoint)) {
            if (index != nullptr) {
                *index = i;
            }
            return &guides[i];
        }
    }
    return nullptr;
}

std::optional<DashboardRenderer::LayoutWidgetIdentity> DashboardApp::HitTestEditableWidget(POINT clientPoint) const {
    return renderer_.HitTestEditableWidget(clientPoint);
}

std::optional<DashboardRenderer::EditableTextKey> DashboardApp::HitTestEditableText(POINT clientPoint) const {
    return renderer_.HitTestEditableText(clientPoint);
}

std::optional<DashboardRenderer::EditableTextKey> DashboardApp::HitTestEditableTextAnchor(POINT clientPoint) const {
    return renderer_.HitTestEditableTextAnchor(clientPoint);
}

void DashboardApp::RefreshLayoutEditHover(POINT clientPoint) {
    if (!isEditingLayout_ || activeLayoutDrag_.has_value() || activeWidgetEditDrag_.has_value() || activeTextEditDrag_.has_value()) {
        return;
    }
    const std::optional<DashboardRenderer::EditableTextKey> nextHoveredTextAnchor = HitTestEditableTextAnchor(clientPoint);
    std::optional<DashboardRenderer::EditableTextKey> nextHoveredText = nextHoveredTextAnchor;
    if (!nextHoveredText.has_value()) {
        nextHoveredText = HitTestEditableText(clientPoint);
    }
    const std::optional<DashboardRenderer::LayoutWidgetIdentity> nextHoveredWidget = nextHoveredText.has_value()
        ? std::optional<DashboardRenderer::LayoutWidgetIdentity>(nextHoveredText->widget)
        : HitTestEditableWidget(clientPoint);
    bool hoverChanged = (hoveredEditableWidget_.has_value() != nextHoveredWidget.has_value());
    if (!hoverChanged && hoveredEditableWidget_.has_value() && nextHoveredWidget.has_value()) {
        hoverChanged = !WidgetIdentityEquals(*hoveredEditableWidget_, *nextHoveredWidget);
    }
    if (hoverChanged) {
        hoveredEditableWidget_ = nextHoveredWidget;
        renderer_.SetHoveredEditableWidget(hoveredEditableWidget_);
    }
    if (hoveredEditableText_.has_value() != nextHoveredText.has_value() ||
        (hoveredEditableText_.has_value() && nextHoveredText.has_value() &&
            !EditableTextKeyEquals(*hoveredEditableText_, *nextHoveredText))) {
        hoveredEditableText_ = nextHoveredText;
        renderer_.SetHoveredEditableText(hoveredEditableText_);
        hoverChanged = true;
    }
    if (hoveredEditableTextAnchor_.has_value() != nextHoveredTextAnchor.has_value() ||
        (hoveredEditableTextAnchor_.has_value() && nextHoveredTextAnchor.has_value() &&
            !EditableTextKeyEquals(*hoveredEditableTextAnchor_, *nextHoveredTextAnchor))) {
        hoveredEditableTextAnchor_ = nextHoveredTextAnchor;
        hoverChanged = true;
    }

    size_t widgetGuideIndex = 0;
    const DashboardRenderer::WidgetEditGuide* widgetGuide = nullptr;
    if (hoveredEditableWidget_.has_value()) {
        widgetGuide = HitTestWidgetEditGuide(clientPoint, &widgetGuideIndex);
        if (widgetGuide != nullptr && !WidgetIdentityEquals(widgetGuide->widget, *hoveredEditableWidget_)) {
            widgetGuide = nullptr;
        }
    }
    const std::optional<size_t> nextWidgetGuideIndex = widgetGuide != nullptr
        ? std::optional<size_t>(widgetGuideIndex)
        : std::nullopt;
    if (hoveredWidgetEditGuideIndex_ != nextWidgetGuideIndex) {
        hoveredWidgetEditGuideIndex_ = nextWidgetGuideIndex;
        hoverChanged = true;
    }

    size_t layoutGuideIndex = 0;
    const DashboardRenderer::LayoutEditGuide* layoutGuide = HitTestLayoutGuide(clientPoint, &layoutGuideIndex);
    const std::optional<size_t> nextLayoutGuideIndex = layoutGuide != nullptr
        ? std::optional<size_t>(layoutGuideIndex)
        : std::nullopt;
    if (hoveredLayoutGuideIndex_ != nextLayoutGuideIndex) {
        hoveredLayoutGuideIndex_ = nextLayoutGuideIndex;
        hoverChanged = true;
    }

    if (hoverChanged) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (hoveredEditableTextAnchor_.has_value()) {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
    } else if (widgetGuide != nullptr) {
        SetCursor(LoadCursorW(nullptr,
            widgetGuide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
    } else if (layoutGuide != nullptr) {
        SetCursor(LoadCursorW(nullptr,
            layoutGuide->axis == DashboardRenderer::LayoutGuideAxis::Vertical ? IDC_SIZEWE : IDC_SIZENS));
    } else {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
}

bool DashboardApp::ApplyLayoutGuideWeights(const DashboardRenderer::LayoutEditGuide& guide, const std::vector<int>& weights) {
    if (!ApplyLayoutGuideWeightsToConfig(config_, guide, weights)) {
        return false;
    }

    renderer_.SetConfig(config_);
    telemetry_->SetEffectiveConfig(config_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool DashboardApp::ApplyWidgetEditValue(const DashboardRenderer::WidgetEditGuide& guide, int value) {
    const int clampedValue = std::max(1, value);
    switch (guide.parameter) {
    case DashboardRenderer::WidgetEditParameter::MetricListLabelWidth:
        config_.layout.metricList.labelWidth = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::MetricListVerticalGap:
        config_.layout.metricList.verticalGap = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth:
        config_.layout.driveUsageList.activityWidth = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth:
        config_.layout.driveUsageList.freeWidth = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::DriveUsageHeaderGap:
        config_.layout.driveUsageList.headerGap = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::DriveUsageRowGap:
        config_.layout.driveUsageList.rowGap = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::ThroughputAxisPadding:
        config_.layout.throughput.axisPadding = clampedValue;
        break;
    case DashboardRenderer::WidgetEditParameter::ThroughputHeaderGap:
        config_.layout.throughput.headerGap = clampedValue;
        break;
    default:
        return false;
    }

    renderer_.SetConfig(config_);
    telemetry_->SetEffectiveConfig(config_);
    if (hoveredEditableWidget_.has_value()) {
        renderer_.SetHoveredEditableWidget(hoveredEditableWidget_);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

bool DashboardApp::ApplyTextEditValue(const DashboardRenderer::EditableTextKey& key, int value) {
    const int clampedValue = std::max(1, value);
    switch (key.fontRole) {
    case DashboardRenderer::FontRole::Title:
        config_.layout.fonts.title.size = clampedValue;
        break;
    case DashboardRenderer::FontRole::Big:
        config_.layout.fonts.big.size = clampedValue;
        break;
    case DashboardRenderer::FontRole::Value:
        config_.layout.fonts.value.size = clampedValue;
        break;
    case DashboardRenderer::FontRole::Label:
        config_.layout.fonts.label.size = clampedValue;
        break;
    case DashboardRenderer::FontRole::Small:
        config_.layout.fonts.smallText.size = clampedValue;
        break;
    default:
        return false;
    }

    renderer_.SetConfig(config_);
    telemetry_->SetEffectiveConfig(config_);
    if (hoveredEditableWidget_.has_value()) {
        renderer_.SetHoveredEditableWidget(hoveredEditableWidget_);
    }
    if (hoveredEditableText_.has_value()) {
        renderer_.SetHoveredEditableText(hoveredEditableText_);
    }
    if (activeTextEditDrag_.has_value()) {
        renderer_.SetActiveEditableText(activeTextEditDrag_->key);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

std::optional<int> DashboardApp::EvaluateLayoutWidgetExtentForWeights(const DashboardRenderer::LayoutEditGuide& guide,
    const std::vector<int>& weights, const DashboardRenderer::LayoutWidgetIdentity& widget, DashboardRenderer::LayoutGuideAxis axis) {
    AppConfig candidateConfig = config_;
    if (!ApplyLayoutGuideWeightsToConfig(candidateConfig, guide, weights)) {
        return std::nullopt;
    }
    renderer_.SetConfig(candidateConfig);
    return renderer_.FindLayoutWidgetExtent(widget, axis);
}

std::optional<std::vector<int>> DashboardApp::FindSnappedLayoutGuideWeights(
    const LayoutDragState& drag, const std::vector<int>& freeWeights) {
    const int threshold = renderer_.LayoutSimilarityThreshold();
    if (threshold <= 0 || drag.snapCandidates.empty()) {
        return std::nullopt;
    }

    const size_t index = drag.guide.separatorIndex;
    if (index + 1 >= freeWeights.size()) {
        return std::nullopt;
    }

    const int combined = freeWeights[index] + freeWeights[index + 1];
    if (combined <= 1) {
        return std::nullopt;
    }

    for (const auto& candidate : drag.snapCandidates) {
        const auto snappedWeight = layout_snap_solver::FindNearestSnapWeight(
            freeWeights[index], combined, threshold, {layout_snap_solver::SnapCandidate{
                candidate.targetExtent,
                candidate.startDistance,
                candidate.groupOrder,
            }}, [&](int firstWeight) -> std::optional<int> {
                std::vector<int> attemptWeights = freeWeights;
                attemptWeights[index] = firstWeight;
                attemptWeights[index + 1] = combined - firstWeight;
                return EvaluateLayoutWidgetExtentForWeights(drag.guide, attemptWeights, candidate.widget, drag.guide.axis);
            });
        if (!snappedWeight.has_value()) {
            continue;
        }

        std::vector<int> exact = freeWeights;
        exact[index] = *snappedWeight;
        exact[index + 1] = combined - *snappedWeight;
        return exact;
    }

    return std::nullopt;
}

bool DashboardApp::UpdateLayoutDrag(POINT clientPoint) {
    if (!activeLayoutDrag_.has_value()) {
        return false;
    }

    LayoutDragState& drag = *activeLayoutDrag_;
    const int currentCoordinate = drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
    const int delta = currentCoordinate - drag.dragStartCoordinate;
    const size_t index = drag.guide.separatorIndex;
    if (index + 1 >= drag.initialWeights.size()) {
        return false;
    }

    const int combined = drag.initialWeights[index] + drag.initialWeights[index + 1];
    if (combined <= 1) {
        return false;
    }

    std::vector<int> weights = drag.initialWeights;
    weights[index] = std::clamp(drag.initialWeights[index] + delta, 1, combined - 1);
    weights[index + 1] = combined - weights[index];
    if ((GetKeyState(VK_MENU) & 0x8000) == 0) {
        if (const auto snappedWeights = FindSnappedLayoutGuideWeights(drag, weights); snappedWeights.has_value()) {
            weights = *snappedWeights;
        }
    }
    if (!ApplyLayoutGuideWeights(drag.guide, weights)) {
        return false;
    }
    const auto& guides = renderer_.LayoutEditGuides();
    const auto guideIt = std::find_if(guides.begin(), guides.end(), [&](const DashboardRenderer::LayoutEditGuide& candidate) {
        return candidate.renderCardId == drag.guide.renderCardId &&
            candidate.editCardId == drag.guide.editCardId &&
            candidate.nodePath == drag.guide.nodePath &&
            candidate.separatorIndex == drag.guide.separatorIndex;
    });
    if (guideIt != guides.end()) {
        drag.guide = *guideIt;
        renderer_.SetActiveLayoutEditGuide(std::optional<DashboardRenderer::LayoutEditGuide>(drag.guide));
    }

    RefreshLayoutEditHover(clientPoint);
    return true;
}

bool DashboardApp::UpdateWidgetEditDrag(POINT clientPoint) {
    if (!activeWidgetEditDrag_.has_value()) {
        return false;
    }

    WidgetEditDragState& drag = *activeWidgetEditDrag_;
    const int currentCoordinate = drag.guide.axis == DashboardRenderer::LayoutGuideAxis::Vertical ? clientPoint.x : clientPoint.y;
    const int pixelDelta = currentCoordinate - drag.dragStartCoordinate;
    const int logicalDelta = static_cast<int>(std::lround(
        static_cast<double>(pixelDelta * drag.guide.dragDirection) / std::max(0.1, renderer_.RenderScale())));
    const int nextValue = std::max(1, drag.initialValue + logicalDelta);
    if (!ApplyWidgetEditValue(drag.guide, nextValue)) {
        return false;
    }

    const auto& guides = renderer_.WidgetEditGuides();
    const auto guideIt = std::find_if(guides.begin(), guides.end(), [&](const DashboardRenderer::WidgetEditGuide& candidate) {
        return candidate.parameter == drag.guide.parameter &&
            candidate.guideId == drag.guide.guideId &&
            candidate.widget.renderCardId == drag.guide.widget.renderCardId &&
            candidate.widget.editCardId == drag.guide.widget.editCardId &&
            candidate.widget.nodePath == drag.guide.widget.nodePath;
    });
    if (guideIt != guides.end()) {
        drag.guide = *guideIt;
        renderer_.SetActiveWidgetEditGuide(drag.guide);
    }

    return true;
}

bool DashboardApp::UpdateTextEditDrag(POINT clientPoint) {
    if (!activeTextEditDrag_.has_value()) {
        return false;
    }

    TextEditDragState& drag = *activeTextEditDrag_;
    const int pixelDelta = clientPoint.x - drag.dragStartCoordinate;
    const int logicalDelta = static_cast<int>(std::lround(
        static_cast<double>(pixelDelta) / std::max(0.1, renderer_.RenderScale())));
    const int nextValue = std::max(1, drag.initialValue + logicalDelta);
    return ApplyTextEditValue(drag.key, nextValue);
}

void DashboardApp::UpdateConfigFromCurrentPlacement() {
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    AppConfig config = BuildCurrentConfigForSaving();
    bool saved = false;
    if (CanWriteRuntimeConfig(configPath)) {
        saved = SaveConfig(configPath, config);
    }
    if (!saved) {
        saved = SaveConfigElevated(configPath, config, hwnd_);
    }
    if (!saved) {
        const std::wstring message = WideFromUtf8("Failed to save " + Utf8FromWide(configPath.wstring()) + ".");
        MessageBoxW(hwnd_, message.c_str(), L"System Telemetry", MB_ICONERROR);
        return;
    }

    config_ = config;
    renderer_.SetConfig(config_);
    telemetry_->SetEffectiveConfig(config_);
}

bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner) {
    const std::filesystem::path tempPath = CreateElevatedSaveConfigTempPath();
    if (tempPath.empty() || targetPath.empty()) {
        return false;
    }

    if (!SaveConfig(tempPath, config)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    std::wstring parameters = L"/save-config \"";
    parameters += tempPath.wstring();
    parameters += L"\" /save-config-target \"";
    parameters += targetPath.wstring();
    parameters += L"\"";

    SHELLEXECUTEINFOW executeInfo{};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.hwnd = owner;
    executeInfo.lpVerb = L"runas";
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }
    const std::wstring executablePath = modulePath;
    executeInfo.lpFile = executablePath.c_str();
    executeInfo.lpParameters = parameters.c_str();
    executeInfo.nShow = SW_HIDE;
    if (!ShellExecuteExW(&executeInfo)) {
        std::error_code ignored;
        std::filesystem::remove(tempPath, ignored);
        return false;
    }

    WaitForSingleObject(executeInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(executeInfo.hProcess, &exitCode);
    CloseHandle(executeInfo.hProcess);

    std::error_code ignored;
    std::filesystem::remove(tempPath, ignored);
    return exitCode == 0;
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
    if (isEditingLayout_) {
        StopLayoutEditMode();
    }
    isMoving_ = true;
    SetTimer(hwnd_, kMoveTimerId, kMoveTimerMs, nullptr);
    UpdateMoveTracking();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DashboardApp::StopMoveMode() {
    if (!isMoving_) {
        return;
    }
    isMoving_ = false;
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

