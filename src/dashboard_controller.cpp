#include "dashboard_controller.h"

#include <algorithm>
#include <fstream>

#include "app_autostart.h"
#include "app_config_io.h"
#include "app_display_config.h"
#include "app_paths.h"
#include "config_writer.h"
#include "layout_edit_service.h"

namespace {

std::unique_ptr<DiagnosticsSession> CreateDiagnosticsSession(const DiagnosticsOptions& options) {
    auto session = std::make_unique<DiagnosticsSession>(options);
    if (!session->Initialize()) {
        return nullptr;
    }
    return session;
}

bool SaveRuntimeConfig(const std::filesystem::path& path, const AppConfig& config, HWND owner) {
    if (CanWriteRuntimeConfig(path)) {
        return SaveConfig(path, config);
    }
    return SaveConfigElevated(path, config, owner);
}

}  // namespace

DashboardController::DashboardController() = default;

DashboardSessionState& DashboardController::State() {
    return state_;
}

const DashboardSessionState& DashboardController::State() const {
    return state_;
}

void DashboardController::SyncRenderer(DashboardShellHost& shell, bool showLayoutEditGuides) {
    shell.Renderer().SetConfig(state_.config);
    shell.RendererEditOverlayState().showLayoutEditGuides = showLayoutEditGuides;
}

void DashboardController::SyncRuntimeAndRenderer(DashboardShellHost& shell, bool showLayoutEditGuides) {
    SyncRenderer(shell, showLayoutEditGuides);
    if (state_.telemetry != nullptr) {
        state_.telemetry->SetEffectiveConfig(state_.config);
    }
}

bool DashboardController::ApplyConfiguredWallpaper() {
    return ::ApplyConfiguredWallpaper(
        state_.config, state_.diagnostics != nullptr ? state_.diagnostics->TraceStream() : nullptr);
}

bool DashboardController::InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) {
    state_.config = LoadRuntimeConfig(diagnosticsOptions);
    if (!ApplyDiagnosticsLayoutOverride(state_.config, diagnosticsOptions)) {
        return false;
    }
    shell.RendererEditOverlayState().similarityIndicatorMode = GetSimilarityIndicatorMode(diagnosticsOptions);
    shell.Renderer().SetTraceOutput(nullptr);

    if (diagnosticsOptions.HasAnyOutput()) {
        state_.diagnostics = CreateDiagnosticsSession(diagnosticsOptions);
        if (state_.diagnostics == nullptr) {
            return false;
        }
        state_.diagnostics->WriteTraceMarker("diagnostics:ui_start");
        state_.diagnostics->WriteTraceMarker("diagnostics:telemetry_initialize_begin");
    }

    state_.telemetry = InitializeTelemetryRuntimeInstance(
        state_.config, diagnosticsOptions, state_.diagnostics != nullptr ? state_.diagnostics->TraceStream() : nullptr);
    if (state_.telemetry == nullptr) {
        if (state_.diagnostics != nullptr) {
            state_.diagnostics->WriteTraceMarker("diagnostics:telemetry_initialize_failed");
        }
        return false;
    }

    if (state_.diagnostics != nullptr) {
        state_.diagnostics->WriteTraceMarker("diagnostics:telemetry_initialized");
        shell.Renderer().SetTraceOutput(state_.diagnostics->TraceStream());
        state_.lastDiagnosticsOutput = std::chrono::steady_clock::now();
    }

    state_.config = state_.telemetry->EffectiveConfig();
    SyncRenderer(shell, diagnosticsOptions.editLayout);
    state_.isEditingLayout = diagnosticsOptions.editLayout;
    ApplyConfiguredWallpaper();
    return true;
}

bool DashboardController::HandleRefreshTimer(DashboardShellHost& shell) {
    if (state_.telemetry == nullptr) {
        return false;
    }
    state_.telemetry->UpdateSnapshot();
    if (state_.diagnostics != nullptr &&
        std::chrono::steady_clock::now() - state_.lastDiagnosticsOutput >= std::chrono::seconds(1)) {
        if (!WriteDiagnosticsOutputs()) {
            return false;
        }
        state_.lastDiagnosticsOutput = std::chrono::steady_clock::now();
    }
    shell.InvalidateShell();
    return true;
}

bool DashboardController::WriteDiagnosticsOutputs() {
    if (state_.diagnostics == nullptr || state_.telemetry == nullptr) {
        return true;
    }
    state_.diagnostics->WriteTraceMarker("diagnostics:write_outputs_begin");
    const bool ok = state_.diagnostics->WriteOutputs(state_.telemetry->Dump(), state_.telemetry->EffectiveConfig());
    state_.diagnostics->WriteTraceMarker(ok ? "diagnostics:write_outputs_done" : "diagnostics:write_outputs_failed");
    return ok;
}

bool DashboardController::ReloadConfigFromDisk(
    DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions, LayoutEditController& controller) {
    StopLayoutEditMode(shell, controller, diagnosticsOptions.editLayout);
    if (!ReloadTelemetryRuntimeFromDisk(
            GetRuntimeConfigPath(), state_.config, state_.telemetry, diagnosticsOptions, state_.diagnostics.get())) {
        shell.ReleaseFonts();
        shell.InitializeFonts();
        return false;
    }
    shell.ReleaseFonts();
    SyncRenderer(shell, diagnosticsOptions.editLayout);
    shell.Renderer().SetTraceOutput(state_.diagnostics != nullptr ? state_.diagnostics->TraceStream() : nullptr);
    if (!shell.InitializeFonts()) {
        if (state_.diagnostics != nullptr) {
            state_.diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    ApplyConfiguredWallpaper();
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.InvalidateShell();
    return true;
}

void DashboardController::SaveDumpAs(DashboardShellHost& shell) {
    if (state_.telemetry == nullptr) {
        return;
    }
    const auto path = shell.PromptDiagnosticsSavePath(
        kDefaultDumpFileName, L"Telemetry dump (*.txt)\0*.txt\0All files (*.*)\0*.*\0", L"txt");
    if (!path.has_value()) {
        return;
    }
    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        shell.ShowError(WideFromUtf8("Failed to open dump file:\n" + Utf8FromWide(path->wstring())));
        return;
    }
    if (!WriteTelemetryDump(output, state_.telemetry->Dump())) {
        shell.ShowError(WideFromUtf8("Failed to write dump file:\n" + Utf8FromWide(path->wstring())));
    }
}

void DashboardController::SaveScreenshotAs(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) {
    if (state_.telemetry == nullptr) {
        return;
    }
    const auto path = shell.PromptDiagnosticsSavePath(
        kDefaultScreenshotFileName, L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0", L"png");
    if (!path.has_value()) {
        return;
    }
    std::string errorText;
    if (!SaveDumpScreenshot(*path,
            state_.telemetry->Dump().snapshot,
            state_.telemetry->EffectiveConfig(),
            shell.CurrentRenderScale(),
            GetDiagnosticsRenderMode(diagnosticsOptions),
            state_.isEditingLayout || diagnosticsOptions.editLayout,
            GetSimilarityIndicatorMode(diagnosticsOptions),
            diagnosticsOptions.editLayoutWidgetName,
            state_.diagnostics != nullptr ? state_.diagnostics->TraceStream() : nullptr,
            &errorText)) {
        std::string message = "Failed to save screenshot:\n" + Utf8FromWide(path->wstring());
        if (!errorText.empty()) {
            message += "\n\n" + errorText;
        }
        shell.ShowError(WideFromUtf8(message));
    }
}

void DashboardController::SaveFullConfigAs(DashboardShellHost& shell) {
    const auto path = shell.PromptDiagnosticsSavePath(
        kDefaultSavedFullConfigFileName, L"INI config (*.ini)\0*.ini\0All files (*.*)\0*.*\0", L"ini");
    if (!path.has_value()) {
        return;
    }
    if (!SaveFullConfig(*path, BuildCurrentConfigForSaving(shell))) {
        shell.ShowError(WideFromUtf8("Failed to save full config file:\n" + Utf8FromWide(path->wstring())));
    }
}

bool DashboardController::IsAutoStartEnabled() const {
    return IsAutoStartEnabledForCurrentExecutable();
}

void DashboardController::ToggleAutoStart(DashboardShellHost& shell) {
    const bool enable = !IsAutoStartEnabled();
    if (!UpdateAutoStartRegistration(enable, shell.WindowHandle())) {
        std::wstring message = L"Failed to ";
        message += enable ? L"enable" : L"disable";
        message += L" auto-start on user logon.";
        shell.ShowError(message);
    }
}

bool DashboardController::ConfigureDisplay(DashboardShellHost& shell, const DisplayMenuOption& option) {
    if (state_.telemetry == nullptr || !option.layoutFits || option.fittedScale <= 0.0) {
        return false;
    }

    AppConfig updatedConfig = state_.telemetry->EffectiveConfig();
    updatedConfig.display.monitorName = option.configMonitorName;
    updatedConfig.display.position = {};
    updatedConfig.display.scale = option.fittedScale;
    updatedConfig.display.wallpaper = Utf8FromWide(kDefaultBlankWallpaperFileName);
    if (!::ConfigureDisplay(updatedConfig,
            state_.telemetry->Dump(),
            option.fittedScale,
            state_.diagnostics != nullptr ? state_.diagnostics->TraceStream() : nullptr,
            shell.WindowHandle())) {
        shell.ShowError(L"Failed to configure the selected display.");
        return false;
    }

    state_.config = updatedConfig;
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    ApplyConfiguredWallpaper();
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.InvalidateShell();
    return true;
}

bool DashboardController::SwitchLayout(DashboardShellHost& shell,
    const std::string& layoutName,
    LayoutEditController& controller,
    bool diagnosticsEditLayout) {
    StopLayoutEditMode(shell, controller, diagnosticsEditLayout);
    AppConfig updatedConfig = state_.config;
    if (!SelectLayout(updatedConfig, layoutName)) {
        return false;
    }

    const AppConfig previousConfig = state_.config;
    shell.ReleaseFonts();
    state_.config = updatedConfig;
    if (state_.telemetry != nullptr) {
        state_.telemetry->SetEffectiveConfig(state_.config);
    }
    SyncRenderer(shell, diagnosticsEditLayout);
    if (!shell.InitializeFonts()) {
        state_.config = previousConfig;
        SyncRuntimeAndRenderer(shell, diagnosticsEditLayout);
        shell.InitializeFonts();
        return false;
    }

    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.InvalidateShell();
    return true;
}

bool DashboardController::SetDisplayScale(DashboardShellHost& shell, double scale) {
    const MonitorPlacementInfo placement = shell.GetWindowPlacementInfo();
    AppConfig updatedConfig = state_.config;
    updatedConfig.display.monitorName =
        !placement.configMonitorName.empty() ? placement.configMonitorName : placement.deviceName;
    const double targetScale = HasExplicitDisplayScale(scale) ? scale : ScaleFromDpi(placement.dpi);
    updatedConfig.display.position.x = ScalePhysicalToLogical(placement.physicalRelativePosition.x, targetScale);
    updatedConfig.display.position.y = ScalePhysicalToLogical(placement.physicalRelativePosition.y, targetScale);
    updatedConfig.display.scale = HasExplicitDisplayScale(scale) ? scale : 0.0;
    state_.config = std::move(updatedConfig);
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.InvalidateShell();
    return true;
}

void DashboardController::SelectNetworkAdapter(DashboardShellHost& shell, const NetworkMenuOption& option) {
    if (state_.telemetry == nullptr) {
        return;
    }
    state_.config.network.adapterName = option.adapterName;
    state_.telemetry->SetPreferredNetworkAdapterName(option.adapterName);
    state_.config = state_.telemetry->EffectiveConfig();
    SyncRenderer(shell, state_.isEditingLayout);
    state_.telemetry->UpdateSnapshot();
    shell.InvalidateShell();
}

void DashboardController::ToggleStorageDrive(DashboardShellHost& shell, const StorageDriveMenuOption& option) {
    if (state_.telemetry == nullptr) {
        return;
    }
    std::vector<std::string> driveLetters = state_.config.storage.drives;
    const auto it = std::find(driveLetters.begin(), driveLetters.end(), option.driveLetter);
    if (it == driveLetters.end()) {
        driveLetters.push_back(option.driveLetter);
    } else {
        driveLetters.erase(it);
    }
    std::sort(driveLetters.begin(), driveLetters.end());
    state_.config.storage.drives = driveLetters;
    state_.telemetry->SetSelectedStorageDrives(driveLetters);
    state_.config = state_.telemetry->EffectiveConfig();
    SyncRenderer(shell, state_.isEditingLayout);
    state_.telemetry->UpdateSnapshot();
    shell.InvalidateShell();
}

void DashboardController::RefreshTelemetrySelections(DashboardShellHost& shell) {
    if (state_.telemetry == nullptr) {
        return;
    }
    state_.telemetry->RefreshSelectionsAndSnapshot();
    state_.config = state_.telemetry->EffectiveConfig();
    SyncRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
}

void DashboardController::StartLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller) {
    if (state_.isEditingLayout) {
        return;
    }
    state_.isEditingLayout = true;
    shell.RendererEditOverlayState().showLayoutEditGuides = true;
    controller.StartSession();
    shell.InvalidateShell();
}

void DashboardController::StopLayoutEditMode(
    DashboardShellHost& shell, LayoutEditController& controller, bool diagnosticsEditLayout) {
    if (!state_.isEditingLayout) {
        return;
    }
    state_.isEditingLayout = false;
    controller.StopSession(diagnosticsEditLayout);
    shell.RendererEditOverlayState().showLayoutEditGuides = diagnosticsEditLayout;
}

bool DashboardController::ApplyLayoutGuideWeights(
    DashboardShellHost& shell, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    if (!layout_edit::ApplyGuideWeights(state_.config, target, weights)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    return true;
}

bool DashboardController::ApplyLayoutEditValue(
    DashboardShellHost& shell, const LayoutEditHost::ValueTarget& target, double value) {
    if (!layout_edit::ApplyValue(state_.config, target, value)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    return true;
}

std::optional<int> DashboardController::EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const DashboardRenderer::LayoutWidgetIdentity& widget,
    DashboardRenderer::LayoutGuideAxis axis) {
    return layout_edit::EvaluateWidgetExtentForGuideWeights(
        shell.Renderer(), state_.config, target, weights, widget, axis);
}

AppConfig DashboardController::BuildCurrentConfigForSaving(DashboardShellHost& shell) const {
    AppConfig config = state_.telemetry != nullptr ? state_.telemetry->EffectiveConfig() : state_.config;
    const MonitorPlacementInfo placement = shell.GetWindowPlacementInfo();
    config.display.monitorName =
        !placement.configMonitorName.empty() ? placement.configMonitorName : placement.deviceName;
    config.display.position.x = placement.relativePosition.x;
    config.display.position.y = placement.relativePosition.y;
    return config;
}

void DashboardController::UpdateConfigFromCurrentPlacement(DashboardShellHost& shell) {
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    AppConfig config = BuildCurrentConfigForSaving(shell);
    if (!SaveRuntimeConfig(configPath, config, shell.WindowHandle())) {
        shell.ShowError(WideFromUtf8("Failed to save " + Utf8FromWide(configPath.wstring()) + "."));
        return;
    }
    state_.config = config;
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
}
