#include "dashboard/dashboard_controller.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string_view>

#include "config/config_resolution.h"
#include "config/config_writer.h"
#include "diagnostics/constants.h"
#include "display/constants.h"
#include "display/display_config.h"
#include "layout_edit/layout_edit_parameter_metadata.h"
#include "layout_edit/layout_edit_service.h"
#include "main/autostart.h"
#include "main/config_io.h"

namespace {

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

std::unique_ptr<DiagnosticsSession> CreateDiagnosticsSession(const DiagnosticsOptions& options, Trace& trace) {
    auto session = std::make_unique<DiagnosticsSession>(options, trace);
    if (!session->Initialize()) {
        return nullptr;
    }
    return session;
}

bool SaveRuntimeConfig(const std::filesystem::path& path, const AppConfig& config, HWND owner) {
    if (CanWriteRuntimeConfig(path)) {
        return SaveConfig(path, config, RuntimeConfigParseContext());
    }
    return SaveConfigElevated(path, config, owner);
}

double ClampGaugeSegmentGapForCurrentConfig(const AppConfig& config, double value) {
    const double totalSweep = std::clamp(config.layout.gauge.sweepDegrees, 0.0, 360.0);
    const int segmentCount = (std::max)(1, config.layout.gauge.segmentCount);
    if (segmentCount <= 1) {
        return 0.0;
    }

    const double minSegmentSweep = (std::min)(0.25, totalSweep / static_cast<double>(segmentCount));
    const double maxSegmentGap = (std::max)(0.0,
        (totalSweep - (minSegmentSweep * static_cast<double>(segmentCount))) / static_cast<double>(segmentCount - 1));
    return std::clamp(value, 0.0, maxSegmentGap);
}

double ClampDriveUsageActivitySegmentGapForCurrentConfig(const AppConfig& config, double value) {
    const int segmentCount = (std::max)(1, config.layout.driveUsageList.activitySegments);
    if (segmentCount <= 1) {
        return 0.0;
    }

    const int rowContentHeight = (std::max)(config.layout.fonts.label.size,
        (std::max)(config.layout.fonts.smallText.size, config.layout.driveUsageList.barHeight));
    const int maxGap = (std::max)(0, (rowContentHeight - segmentCount) / (segmentCount - 1));
    return static_cast<double>(std::clamp((std::max)(0, static_cast<int>(std::lround(value))), 0, maxGap));
}

}  // namespace

DashboardController::DashboardController() = default;

DashboardSessionState& DashboardController::State() {
    return state_;
}

const DashboardSessionState& DashboardController::State() const {
    return state_;
}

void DashboardController::BeginLayoutEditSessionTracking() {
    state_.layoutEditSessionSavedLayout = state_.config.layout;
    state_.hasLayoutEditSessionSavedLayout = true;
    state_.hasUnsavedLayoutEditChanges = false;
}

void DashboardController::ClearLayoutEditSessionTracking() {
    state_.hasLayoutEditSessionSavedLayout = false;
    state_.hasUnsavedLayoutEditChanges = false;
    state_.layoutEditSessionSavedLayout = LayoutConfig{};
}

void DashboardController::RefreshLayoutEditSessionDirtyFlag() {
    if (!state_.isEditingLayout || !state_.hasLayoutEditSessionSavedLayout) {
        state_.hasUnsavedLayoutEditChanges = false;
        return;
    }
    state_.hasUnsavedLayoutEditChanges = state_.config.layout != state_.layoutEditSessionSavedLayout;
}

void DashboardController::MarkLayoutEditSessionSaved() {
    if (!state_.isEditingLayout) {
        return;
    }
    state_.layoutEditSessionSavedLayout = state_.config.layout;
    state_.hasLayoutEditSessionSavedLayout = true;
    state_.hasUnsavedLayoutEditChanges = false;
}

void DashboardController::SyncRenderer(DashboardShellHost& shell, bool showLayoutEditGuides) {
    shell.Renderer().SetConfig(state_.config);
    shell.RendererDashboardOverlayState().showLayoutEditGuides = showLayoutEditGuides;
}

void DashboardController::SyncRuntimeAndRenderer(DashboardShellHost& shell, bool showLayoutEditGuides) {
    SyncRenderer(shell, showLayoutEditGuides);
}

bool DashboardController::ApplyConfiguredWallpaper(Trace& trace) {
    return ::ApplyConfiguredWallpaper(state_.config, trace);
}

bool DashboardController::InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) {
    state_.lastError.clear();
    state_.config = LoadRuntimeConfig(diagnosticsOptions);
    if (!ApplyDiagnosticsLayoutOverride(state_.config, diagnosticsOptions)) {
        return false;
    }
    shell.RendererDashboardOverlayState().similarityIndicatorMode = GetSimilarityIndicatorMode(diagnosticsOptions);

    if (diagnosticsOptions.HasAnyOutput()) {
        state_.diagnostics = CreateDiagnosticsSession(diagnosticsOptions, shell.TraceLog());
        if (state_.diagnostics == nullptr) {
            return false;
        }
        state_.diagnostics->WriteTraceMarker("diagnostics:ui_start");
        state_.diagnostics->WriteTraceMarker("diagnostics:telemetry_initialize_begin");
    }

    std::string telemetryError;
    state_.telemetry =
        InitializeTelemetryCollectorInstance(state_.config, diagnosticsOptions, shell.TraceLog(), &telemetryError);
    if (state_.telemetry == nullptr) {
        if (state_.diagnostics != nullptr) {
            std::string traceText = "diagnostics:telemetry_initialize_failed";
            if (!telemetryError.empty()) {
                traceText += " detail=" + QuoteTraceText(telemetryError);
            }
            state_.diagnostics->WriteTraceMarker(traceText);
        }
        state_.lastError = FormatTelemetryInitializeError(telemetryError);
        return false;
    }

    if (state_.diagnostics != nullptr) {
        state_.diagnostics->WriteTraceMarker("diagnostics:telemetry_initialized");
        state_.lastDiagnosticsOutput = std::chrono::steady_clock::now();
    }

    state_.config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    SyncRenderer(shell, diagnosticsOptions.editLayout);
    state_.isEditingLayout = diagnosticsOptions.editLayout;
    if (state_.isEditingLayout) {
        BeginLayoutEditSessionTracking();
    }
    ApplyConfiguredWallpaper(shell.TraceLog());
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
    const bool ok = state_.diagnostics->WriteOutputs(
        state_.telemetry->Dump(), BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections()));
    state_.diagnostics->WriteTraceMarker(ok ? "diagnostics:write_outputs_done" : "diagnostics:write_outputs_failed");
    return ok;
}

bool DashboardController::ReloadConfigFromDisk(
    DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) {
    std::string telemetryError;
    if (!ReloadTelemetryCollectorFromDisk(GetRuntimeConfigPath(),
            state_.config,
            state_.telemetry,
            diagnosticsOptions,
            shell.TraceLog(),
            state_.diagnostics.get(),
            &telemetryError)) {
        if (!telemetryError.empty() && (state_.diagnostics == nullptr || state_.diagnostics->ShouldShowDialogs())) {
            shell.ShowError(FormatTelemetryInitializeError(telemetryError));
        }
        shell.ReleaseFonts();
        shell.InitializeFonts();
        return false;
    }
    SyncRenderer(shell, state_.isEditingLayout || diagnosticsOptions.editLayout);
    if (!shell.Renderer().LastError().empty()) {
        if (state_.diagnostics != nullptr) {
            state_.diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    ApplyConfiguredWallpaper(shell.TraceLog());
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.RedrawShellNow();
    if (state_.isEditingLayout) {
        MarkLayoutEditSessionSaved();
    }
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
            BuildCurrentConfigForSaving(shell),
            shell.CurrentRenderScale(),
            GetDiagnosticsRenderMode(diagnosticsOptions),
            state_.isEditingLayout || diagnosticsOptions.editLayout,
            GetSimilarityIndicatorMode(diagnosticsOptions),
            diagnosticsOptions.editLayoutWidgetName,
            shell.TraceLog(),
            diagnosticsOptions.hoverPoint.has_value()
                ? std::optional<RenderPoint>(
                      RenderPoint{diagnosticsOptions.hoverPoint->x, diagnosticsOptions.hoverPoint->y})
                : std::nullopt,
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

    AppConfig updatedConfig = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    updatedConfig.display.monitorName = option.configMonitorName;
    updatedConfig.display.position = {};
    updatedConfig.display.scale = option.fittedScale;
    updatedConfig.display.wallpaper = Utf8FromWide(kDefaultBlankWallpaperFileName);
    if (!::ConfigureDisplay(
            updatedConfig, state_.telemetry->Dump(), option.fittedScale, shell.TraceLog(), shell.WindowHandle())) {
        shell.ShowError(L"Failed to configure the selected display.");
        return false;
    }

    state_.config = updatedConfig;
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    ApplyConfiguredWallpaper(shell.TraceLog());
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.RedrawShellNow();
    return true;
}

bool DashboardController::SwitchLayout(
    DashboardShellHost& shell, const std::string& layoutName, bool diagnosticsEditLayout) {
    if (state_.diagnostics != nullptr) {
        state_.diagnostics->WriteTraceMarker(
            "layout_switch:begin current_layout=" + QuoteTraceText(state_.config.display.layout) +
            " requested_layout=" + QuoteTraceText(layoutName));
    }
    AppConfig updatedConfig = state_.config;
    if (!SelectLayout(updatedConfig, layoutName)) {
        if (state_.diagnostics != nullptr) {
            state_.diagnostics->WriteTraceMarker(
                "layout_switch:select_failed requested_layout=" + QuoteTraceText(layoutName));
        }
        return false;
    }

    const AppConfig previousConfig = state_.config;
    state_.config = updatedConfig;
    SyncRenderer(shell, state_.isEditingLayout || diagnosticsEditLayout);
    if (!shell.Renderer().LastError().empty()) {
        if (state_.diagnostics != nullptr) {
            state_.diagnostics->WriteTraceMarker(
                "layout_switch:sync_failed requested_layout=" + QuoteTraceText(layoutName) +
                " renderer_error=" + QuoteTraceText(shell.Renderer().LastError()));
        }
        state_.config = previousConfig;
        SyncRuntimeAndRenderer(shell, state_.isEditingLayout || diagnosticsEditLayout);
        return false;
    }

    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.RedrawShellNow();
    RefreshLayoutEditSessionDirtyFlag();
    if (state_.diagnostics != nullptr) {
        state_.diagnostics->WriteTraceMarker(
            "layout_switch:done active_layout=" + QuoteTraceText(state_.config.display.layout));
    }
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
    shell.RedrawShellNow();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

void DashboardController::SelectNetworkAdapter(DashboardShellHost& shell, const NetworkMenuOption& option) {
    if (state_.telemetry == nullptr) {
        return;
    }
    state_.config.network.adapterName = option.adapterName;
    state_.telemetry->SetPreferredNetworkAdapterName(option.adapterName);
    state_.config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    SyncRenderer(shell, state_.isEditingLayout);
    state_.telemetry->UpdateSnapshot();
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
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
    state_.config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    SyncRenderer(shell, state_.isEditingLayout);
    state_.telemetry->UpdateSnapshot();
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
}

void DashboardController::RefreshTelemetrySelections(DashboardShellHost& shell) {
    if (state_.telemetry == nullptr) {
        return;
    }
    state_.telemetry->RefreshSelectionsAndSnapshot();
    state_.config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    SyncRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    if (state_.isEditingLayout && !state_.hasUnsavedLayoutEditChanges) {
        MarkLayoutEditSessionSaved();
    }
}

void DashboardController::StartLayoutEditMode(DashboardShellHost& shell, LayoutEditController& controller) {
    if (state_.isEditingLayout) {
        return;
    }
    state_.isEditingLayout = true;
    BeginLayoutEditSessionTracking();
    shell.RendererDashboardOverlayState().showLayoutEditGuides = true;
    controller.StartSession();
    shell.InvalidateShell();
}

void DashboardController::StopLayoutEditMode(
    DashboardShellHost& shell, LayoutEditController& controller, bool diagnosticsEditLayout) {
    if (!state_.isEditingLayout) {
        return;
    }
    state_.isEditingLayout = false;
    ClearLayoutEditSessionTracking();
    controller.StopSession(diagnosticsEditLayout);
    shell.RendererDashboardOverlayState().showLayoutEditGuides = diagnosticsEditLayout;
}

bool DashboardController::HasUnsavedLayoutEditChanges() const {
    return state_.isEditingLayout && state_.hasLayoutEditSessionSavedLayout && state_.hasUnsavedLayoutEditChanges;
}

bool DashboardController::RestoreLayoutEditSessionSavedLayout(DashboardShellHost& shell) {
    if (!state_.hasLayoutEditSessionSavedLayout) {
        return false;
    }

    state_.config.layout = state_.layoutEditSessionSavedLayout;
    if (!SelectResolvedLayout(state_.config, state_.config.display.layout)) {
        return false;
    }
    if (state_.telemetry != nullptr) {
        state_.telemetry->SetPreferredNetworkAdapterName(state_.config.network.adapterName);
        state_.telemetry->SetSelectedStorageDrives(state_.config.storage.drives);
        state_.telemetry->RefreshSelectionsAndSnapshot();
        state_.config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    MarkLayoutEditSessionSaved();
    return true;
}

bool DashboardController::ApplyLayoutGuideWeights(
    DashboardShellHost& shell, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    if (!ApplyGuideWeights(state_.config, target, weights)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::ApplyMetricListOrder(
    DashboardShellHost& shell, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) {
    if (!::ApplyMetricListOrder(state_.config, widget, metricRefs)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::ApplyContainerChildOrder(
    DashboardShellHost& shell, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) {
    if (!::ApplyContainerChildOrder(state_.config, key, fromIndex, toIndex)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::ApplyLayoutEditValue(
    DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, double value) {
    double nextValue = value;
    if (parameter == DashboardRenderer::LayoutEditParameter::GaugeSegmentGapDegrees) {
        nextValue = ClampGaugeSegmentGapForCurrentConfig(state_.config, nextValue);
    } else if (parameter == DashboardRenderer::LayoutEditParameter::DriveUsageActivitySegmentGap) {
        nextValue = ClampDriveUsageActivitySegmentGapForCurrentConfig(state_.config, nextValue);
    }
    if (!ApplyLayoutEditParameterValue(state_.config, parameter, nextValue)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::ApplyLayoutEditFont(
    DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, const UiFontConfig& value) {
    if (!ApplyLayoutEditParameterFontValue(state_.config, parameter, value)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::ApplyLayoutEditColor(
    DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, unsigned int value) {
    if (!ApplyLayoutEditParameterColorValue(state_.config, parameter, value)) {
        return false;
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

void DashboardController::ApplyConfigSnapshot(DashboardShellHost& shell, const AppConfig& config) {
    const AppConfig previousConfig = state_.config;
    state_.config = config;
    if (state_.telemetry != nullptr) {
        const TelemetrySettings previousSettings = ExtractTelemetrySettings(previousConfig);
        const TelemetrySettings nextSettings = ExtractTelemetrySettings(state_.config);
        if (previousSettings != nextSettings) {
            state_.telemetry->ApplySettings(nextSettings);
            state_.telemetry->UpdateSnapshot();
            state_.config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
        }
    }
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
}

std::optional<int> DashboardController::EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const LayoutEditWidgetIdentity& widget,
    LayoutGuideAxis axis) {
    return EvaluateWidgetExtentForGuideWeights(shell.Renderer(), target, weights, widget, axis);
}

AppConfig DashboardController::BuildCurrentConfigForSaving(DashboardShellHost& shell) const {
    AppConfig config = state_.config;
    if (state_.telemetry != nullptr) {
        config = BuildEffectiveRuntimeConfig(state_.config, state_.telemetry->ResolvedSelections());
    }
    const MonitorPlacementInfo placement = shell.GetWindowPlacementInfo();
    config.display.monitorName =
        !placement.configMonitorName.empty() ? placement.configMonitorName : placement.deviceName;
    config.display.position.x = placement.relativePosition.x;
    config.display.position.y = placement.relativePosition.y;
    return config;
}

bool DashboardController::UpdateConfigFromCurrentPlacement(DashboardShellHost& shell) {
    const std::filesystem::path configPath = GetRuntimeConfigPath();
    AppConfig config = BuildCurrentConfigForSaving(shell);
    if (!SaveRuntimeConfig(configPath, config, shell.WindowHandle())) {
        shell.ShowError(WideFromUtf8("Failed to save " + Utf8FromWide(configPath.wstring()) + "."));
        return false;
    }
    state_.config = config;
    SyncRuntimeAndRenderer(shell, state_.isEditingLayout);
    if (state_.isEditingLayout) {
        MarkLayoutEditSessionSaved();
    }
    return true;
}
