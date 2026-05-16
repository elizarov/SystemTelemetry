#include "dashboard/dashboard_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config/color_resolver.h"
#include "config/config_io.h"
#include "config/config_resolution.h"
#include "config/config_writer.h"
#include "dashboard/autostart.h"
#include "diagnostics/constants.h"
#include "display/constants.h"
#include "display/display_config.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_edit/layout_edit_service.h"
#include "layout_model/layout_edit_service.h"
#include "telemetry/metrics.h"
#include "util/command_line.h"
#include "util/elevated_process.h"
#include "util/strings.h"
#include "util/temp_file.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

constexpr char kTelemetryDumpFilter[] = "Telemetry dump (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
constexpr char kPngFilter[] = "PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0";
constexpr char kIniFilter[] = "INI config (*.ini)\0*.ini\0All files (*.*)\0*.*\0";
constexpr wchar_t kWriteBinaryMode[] = L"wb";  // _wfopen_s mode string follows the widened export path.

template <size_t Size> constexpr std::string_view StringViewWithTerminator(const char (&text)[Size]) {
    return std::string_view(text, Size);
}

ThemeConfig* FindThemeConfig(LayoutConfig& layout, const std::string& name) {
    for (ThemeConfig& theme : layout.themes) {
        if (theme.name == name) {
            return &theme;
        }
    }
    return nullptr;
}

LayoutCardConfig* FindCardConfig(LayoutConfig& layout, const std::string& id) {
    for (LayoutCardConfig& card : layout.cards) {
        if (card.id == id) {
            return &card;
        }
    }
    return nullptr;
}

std::unique_ptr<DiagnosticsSession> CreateDiagnosticsSession(const DiagnosticsOptions& options, Trace& trace) {
    auto session = std::make_unique<DiagnosticsSession>(options, trace);
    if (!session->Initialize()) {
        return nullptr;
    }
    return session;
}

bool SaveConfigElevated(
    const FilePath& targetPath, const AppConfig& config, HWND owner, const ConfigParseContext& context) {
    const FilePath tempPath = CreateTempFilePath("stc");
    if (tempPath.empty() || targetPath.empty()) {
        return false;
    }
    if (!SaveConfig(tempPath, config, context)) {
        RemoveFileIfExists(tempPath);
        return false;
    }

    std::string parameters = "/save-config ";
    parameters += QuoteCommandLineArgument(tempPath.string());
    parameters += " /save-config-target ";
    parameters += QuoteCommandLineArgument(targetPath.string());

    DWORD exitCode = 1;
    const bool launched = RunElevatedSelfAndWait(owner, parameters, {}, SW_HIDE, &exitCode);
    RemoveFileIfExists(tempPath);
    return launched && exitCode == 0;
}

bool SaveRuntimeConfig(const FilePath& path, const AppConfig& config, HWND owner) {
    if (CanWriteRuntimeConfig(path)) {
        return SaveConfig(path, config, ConfigParseContext{TelemetryMetricCatalog()});
    }
    return SaveConfigElevated(path, config, owner, ConfigParseContext{TelemetryMetricCatalog()});
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

void AssignCommonFontFamily(UiFontSetConfig& fonts, const std::string& family) {
    fonts.title.face = family;
    fonts.big.face = family;
    fonts.value.face = family;
    fonts.label.face = family;
    fonts.text.face = family;
    fonts.smallText.face = family;
    fonts.footer.face = family;
    fonts.clockTime.face = family;
    fonts.clockDate.face = family;
}

ColorConfig* FindLayoutColorConfig(ColorsConfig& colors, DashboardRenderer::LayoutEditParameter parameter) {
    switch (parameter) {
        case DashboardRenderer::LayoutEditParameter::ColorBackground:
            return &colors.backgroundColor;
        case DashboardRenderer::LayoutEditParameter::ColorForeground:
            return &colors.foregroundColor;
        case DashboardRenderer::LayoutEditParameter::ColorIcon:
            return &colors.iconColor;
        case DashboardRenderer::LayoutEditParameter::ColorPeakGhost:
            return &colors.peakGhostColor;
        case DashboardRenderer::LayoutEditParameter::ColorWarning:
            return &colors.warningColor;
        case DashboardRenderer::LayoutEditParameter::ColorAccent:
            return &colors.accentColor;
        case DashboardRenderer::LayoutEditParameter::ColorLayoutGuide:
            return &colors.layoutGuideColor;
        case DashboardRenderer::LayoutEditParameter::ColorActiveEdit:
            return &colors.activeEditColor;
        case DashboardRenderer::LayoutEditParameter::ColorPanelBorder:
            return &colors.panelBorderColor;
        case DashboardRenderer::LayoutEditParameter::ColorMutedText:
            return &colors.mutedTextColor;
        case DashboardRenderer::LayoutEditParameter::ColorTrack:
            return &colors.trackColor;
        case DashboardRenderer::LayoutEditParameter::ColorPanelFill:
            return &colors.panelFillColor;
        case DashboardRenderer::LayoutEditParameter::ColorGraphBackground:
            return &colors.graphBackgroundColor;
        case DashboardRenderer::LayoutEditParameter::ColorGraphAxis:
            return &colors.graphAxisColor;
        case DashboardRenderer::LayoutEditParameter::ColorGraphMarker:
            return &colors.graphMarkerColor;
        default:
            return nullptr;
    }
}

ColorConfig* FindThemeColorConfig(ThemeConfig& theme, const std::string& tokenName) {
    if (tokenName == "background") {
        return &theme.background;
    }
    if (tokenName == "foreground") {
        return &theme.foreground;
    }
    if (tokenName == "accent") {
        return &theme.accent;
    }
    if (tokenName == "guide") {
        return &theme.guide;
    }
    return nullptr;
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
    state_.layoutEditSessionSavedLayout = std::make_unique<LayoutConfig>(state_.config.layout);
    state_.hasUnsavedLayoutEditChanges = false;
}

void DashboardController::ClearLayoutEditSessionTracking() {
    state_.hasUnsavedLayoutEditChanges = false;
    state_.layoutEditSessionSavedLayout.reset();
}

void DashboardController::RefreshLayoutEditSessionDirtyFlag() {
    if (!state_.isEditingLayout || state_.layoutEditSessionSavedLayout == nullptr) {
        state_.hasUnsavedLayoutEditChanges = false;
        return;
    }
    // Size: exact layout diffing reuses config metadata and runs at prompt boundaries, not on every drag mutation.
    state_.hasUnsavedLayoutEditChanges = true;
}

void DashboardController::MarkLayoutEditSessionSaved() {
    if (!state_.isEditingLayout) {
        return;
    }
    state_.layoutEditSessionSavedLayout = std::make_unique<LayoutConfig>(state_.config.layout);
    state_.hasUnsavedLayoutEditChanges = false;
}

void DashboardController::SyncRenderer(DashboardShellHost& shell, bool showLayoutEditGuides, bool refreshThemedIcons) {
    shell.Renderer().SetConfig(state_.config);
    shell.RendererDashboardOverlayState().showLayoutEditGuides = showLayoutEditGuides;
    if (refreshThemedIcons) {
        shell.RefreshThemedIcons();
    }
}

__declspec(noinline) bool DashboardController::FinishConfigMutation(
    DashboardShellHost& shell, bool refreshThemedIcons) {
    // Size: many cold config appliers end with this same UI refresh tail; keep it out of each caller.
    SyncRenderer(shell, state_.isEditingLayout, refreshThemedIcons);
    shell.InvalidateShell();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::ApplyConfiguredWallpaper(Trace& trace) {
    return ::ApplyConfiguredWallpaper(state_.config, trace);
}

bool DashboardController::InitializeSession(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) {
    state_.lastError.clear();
    state_.config = LoadRuntimeConfig(diagnosticsOptions, ConfigParseContext{TelemetryMetricCatalog()});
    if (!ApplyDiagnosticsLayoutOverride(state_.config, diagnosticsOptions)) {
        return false;
    }
    if (!ApplyDiagnosticsThemeOverride(state_.config, diagnosticsOptions)) {
        return false;
    }
    shell.RendererDashboardOverlayState().similarityIndicatorMode = GetSimilarityIndicatorMode(diagnosticsOptions);

    if (diagnosticsOptions.HasAnyOutput()) {
        state_.diagnostics = CreateDiagnosticsSession(diagnosticsOptions, shell.TraceLog());
        if (state_.diagnostics == nullptr) {
            return false;
        }
        state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, "ui_start");
        state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, "telemetry_initialize_begin");
    }

    std::string telemetryError;
    state_.telemetry = InitializeTelemetryRuntimeInstance(
        state_.config, diagnosticsOptions, shell.TraceLog(), &shell, &telemetryError);
    if (state_.telemetry == nullptr) {
        if (state_.diagnostics != nullptr) {
            std::string traceText = "telemetry_initialize_failed";
            if (!telemetryError.empty()) {
                const std::string telemetryErrorText = Trace::QuoteText(telemetryError);
                AppendFormat(traceText, " detail=%s", telemetryErrorText.c_str());
            }
            state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, traceText);
        }
        state_.lastError = FormatTelemetryInitializeError(telemetryError);
        return false;
    }

    state_.telemetryUpdate = state_.telemetry->Latest();
    if (state_.diagnostics != nullptr) {
        state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, "telemetry_initialized");
        state_.lastDiagnosticsOutput = std::chrono::steady_clock::now();
    }

    ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
    SyncRenderer(shell, diagnosticsOptions.editLayout);
    state_.isEditingLayout = diagnosticsOptions.editLayout;
    if (state_.isEditingLayout) {
        BeginLayoutEditSessionTracking();
    }
    ApplyConfiguredWallpaper(shell.TraceLog());
    return true;
}

bool DashboardController::HandleTelemetryUpdate(DashboardShellHost& shell, const TelemetryUpdate& update) {
    if (state_.telemetry == nullptr) {
        return false;
    }
    state_.telemetryUpdate = update;
    ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
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
    state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, "write_outputs_begin");
    const bool ok = state_.diagnostics->WriteOutputs(state_.telemetryUpdate.dump, state_.config);
    state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, ok ? "write_outputs_done" : "write_outputs_failed");
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
            &shell,
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
            state_.diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, "reload_config_failed");
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
    const auto path =
        shell.PromptDiagnosticsSavePath(kDefaultDumpFileName, StringViewWithTerminator(kTelemetryDumpFilter), "txt");
    if (!path.has_value()) {
        return;
    }
    std::FILE* output = nullptr;
    const std::wstring widePath = path->Wide();
    if (_wfopen_s(&output, widePath.c_str(), kWriteBinaryMode) != 0 || output == nullptr) {
        const std::string pathText = path->string();
        shell.ShowError("Failed to open dump file:\n" + pathText);
        return;
    }
    const bool written = WriteTelemetryDump(output, state_.telemetryUpdate.dump);
    fclose(output);
    if (!written) {
        const std::string pathText = path->string();
        shell.ShowError("Failed to write dump file:\n" + pathText);
    }
}

void DashboardController::SaveScreenshotAs(DashboardShellHost& shell, const DiagnosticsOptions& diagnosticsOptions) {
    if (state_.telemetry == nullptr) {
        return;
    }
    const auto path =
        shell.PromptDiagnosticsSavePath(kDefaultScreenshotFileName, StringViewWithTerminator(kPngFilter), "png");
    if (!path.has_value()) {
        return;
    }
    std::string errorText;
    if (!SaveDumpScreenshot(*path,
            state_.telemetryUpdate.dump.snapshot,
            BuildCurrentConfigForSaving(shell),
            shell.CurrentRenderScale(),
            GetDiagnosticsRenderMode(diagnosticsOptions),
            state_.isEditingLayout || diagnosticsOptions.editLayout,
            GetSimilarityIndicatorMode(diagnosticsOptions),
            diagnosticsOptions.editLayoutWidgetName,
            shell.TraceLog(),
            diagnosticsOptions.hoverPoint.has_value(),
            diagnosticsOptions.hoverPoint.has_value()
                ? RenderPoint{diagnosticsOptions.hoverPoint->x, diagnosticsOptions.hoverPoint->y}
                : RenderPoint{},
            &errorText)) {
        const std::string pathText = path->string();
        std::string message = FormatText("Failed to save screenshot:\n%s", pathText.c_str());
        if (!errorText.empty()) {
            AppendFormat(message, "\n\n%s", errorText.c_str());
        }
        shell.ShowError(message);
    }
}

void DashboardController::SaveLayoutGuideSheetAs(DashboardShellHost& shell) {
    if (state_.telemetry == nullptr) {
        return;
    }
    const auto path =
        shell.PromptDiagnosticsSavePath(kDefaultLayoutGuideSheetFileName, StringViewWithTerminator(kPngFilter), "png");
    if (!path.has_value()) {
        return;
    }
    std::string errorText;
    if (!SaveLayoutGuideSheet(*path,
            state_.telemetryUpdate.dump.snapshot,
            BuildCurrentConfigForSaving(shell),
            shell.CurrentRenderScale(),
            shell.TraceLog(),
            &errorText)) {
        const std::string pathText = path->string();
        std::string message = FormatText("Failed to save layout guide sheet:\n%s", pathText.c_str());
        if (!errorText.empty()) {
            AppendFormat(message, "\n\n%s", errorText.c_str());
        }
        shell.ShowError(message);
    }
}

void DashboardController::SaveFullConfigAs(DashboardShellHost& shell) {
    const auto path =
        shell.PromptDiagnosticsSavePath(kDefaultSavedFullConfigFileName, StringViewWithTerminator(kIniFilter), "ini");
    if (!path.has_value()) {
        return;
    }
    if (!SaveFullConfig(*path, BuildCurrentConfigForSaving(shell))) {
        const std::string pathText = path->string();
        shell.ShowError("Failed to save full config file:\n" + pathText);
    }
}

bool DashboardController::IsAutoStartEnabled() const {
    return IsAutoStartEnabledForCurrentExecutable();
}

void DashboardController::ToggleAutoStart(DashboardShellHost& shell) {
    const bool enable = !IsAutoStartEnabled();
    if (!UpdateAutoStartRegistration(enable, shell.WindowHandle())) {
        shell.ShowError(std::string("Failed to ") + (enable ? "enable" : "disable") + " auto-start on user logon.");
    }
}

bool DashboardController::ConfigureDisplay(DashboardShellHost& shell, const DisplayMenuOption& option) {
    if (state_.telemetry == nullptr || !option.layoutFits || option.fittedScale <= 0.0) {
        return false;
    }

    AppConfig updatedConfig = state_.config;
    ApplyResolvedTelemetrySelections(updatedConfig, state_.telemetryUpdate.resolvedSelections);
    updatedConfig.display.monitorName = option.configMonitorName;
    updatedConfig.display.position = {};
    updatedConfig.display.scale = option.fittedScale;
    updatedConfig.display.wallpaper = kDefaultBlankWallpaperFileName;
    if (!::ConfigureDisplay(
            updatedConfig, state_.telemetryUpdate.dump, option.fittedScale, shell.TraceLog(), shell.WindowHandle())) {
        shell.ShowError("Failed to configure the selected display.");
        return false;
    }

    state_.config = std::move(updatedConfig);
    SyncRenderer(shell, state_.isEditingLayout);
    ApplyConfiguredWallpaper(shell.TraceLog());
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.RedrawShellNow();
    return true;
}

bool DashboardController::SwitchLayout(
    DashboardShellHost& shell, const std::string& layoutName, bool diagnosticsEditLayout) {
    if (state_.diagnostics != nullptr) {
        const std::string currentLayoutText = Trace::QuoteText(state_.config.display.layout);
        const std::string requestedLayoutText = Trace::QuoteText(layoutName);
        state_.diagnostics->WriteTraceMarkerFmt(TracePrefix::LayoutSwitch,
            "begin current_layout=%s requested_layout=%s",
            currentLayoutText.c_str(),
            requestedLayoutText.c_str());
    }
    const std::string previousLayoutName = state_.config.display.layout;
    if (!SelectLayout(state_.config, layoutName)) {
        if (state_.diagnostics != nullptr) {
            const std::string requestedLayoutText = Trace::QuoteText(layoutName);
            state_.diagnostics->WriteTraceMarkerFmt(
                TracePrefix::LayoutSwitch, "select_failed requested_layout=%s", requestedLayoutText.c_str());
        }
        return false;
    }

    SyncRenderer(shell, state_.isEditingLayout || diagnosticsEditLayout);
    if (!shell.Renderer().LastError().empty()) {
        if (state_.diagnostics != nullptr) {
            const std::string requestedLayoutText = Trace::QuoteText(layoutName);
            const std::string rendererErrorText = Trace::QuoteText(shell.Renderer().LastError());
            state_.diagnostics->WriteTraceMarkerFmt(TracePrefix::LayoutSwitch,
                "sync_failed requested_layout=%s renderer_error=%s",
                requestedLayoutText.c_str(),
                rendererErrorText.c_str());
        }
        // The active config has already resolved a valid layout; rollback by name avoids a full config snapshot.
        SelectLayout(state_.config, previousLayoutName);
        SyncRenderer(shell, state_.isEditingLayout || diagnosticsEditLayout);
        return false;
    }

    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.RedrawShellNow();
    RefreshLayoutEditSessionDirtyFlag();
    if (state_.diagnostics != nullptr) {
        const std::string activeLayoutText = Trace::QuoteText(state_.config.display.layout);
        state_.diagnostics->WriteTraceMarkerFmt(
            TracePrefix::LayoutSwitch, "done active_layout=%s", activeLayoutText.c_str());
    }
    return true;
}

bool DashboardController::SwitchTheme(
    DashboardShellHost& shell, const std::string& themeName, bool diagnosticsEditLayout) {
    if (FindThemeConfig(state_.config.layout, themeName) == nullptr) {
        return false;
    }

    state_.config.display.theme = themeName;
    ResolveConfiguredColors(state_.config);
    SyncRenderer(shell, state_.isEditingLayout || diagnosticsEditLayout);
    if (!shell.Renderer().LastError().empty()) {
        return false;
    }
    shell.RedrawShellNow();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

bool DashboardController::SetDisplayScale(DashboardShellHost& shell, double scale) {
    const MonitorPlacementInfo placement = shell.GetWindowPlacementInfo();
    state_.config.display.monitorName =
        !placement.configMonitorName.empty() ? placement.configMonitorName : placement.deviceName;
    const double targetScale = HasExplicitDisplayScale(scale) ? scale : ScaleFromDpi(placement.dpi);
    state_.config.display.position.x = ScalePhysicalToLogical(placement.physicalRelativePosition.x, targetScale);
    state_.config.display.position.y = ScalePhysicalToLogical(placement.physicalRelativePosition.y, targetScale);
    state_.config.display.scale = HasExplicitDisplayScale(scale) ? scale : 0.0;
    SyncRenderer(shell, state_.isEditingLayout);
    state_.placementWatchActive = true;
    shell.ApplyConfigPlacement();
    shell.RedrawShellNow();
    RefreshLayoutEditSessionDirtyFlag();
    return true;
}

void DashboardController::SelectNetworkAdapter(DashboardShellHost& shell, const std::string& adapterName) {
    if (state_.telemetry == nullptr) {
        return;
    }
    state_.config.network.adapterName = adapterName;
    state_.telemetry->SetPreferredNetworkAdapterName(adapterName);
    state_.telemetryUpdate = state_.telemetry->Latest();
    ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
    FinishConfigMutation(shell, false);
}

void DashboardController::ToggleStorageDrive(DashboardShellHost& shell, const std::string& driveLetter) {
    if (state_.telemetry == nullptr) {
        return;
    }
    std::vector<std::string> driveLetters = state_.config.storage.drives;
    const auto it = std::find(driveLetters.begin(), driveLetters.end(), driveLetter);
    if (it == driveLetters.end()) {
        driveLetters.push_back(driveLetter);
    } else {
        driveLetters.erase(it);
    }
    SortStrings(driveLetters);
    state_.config.storage.drives = driveLetters;
    state_.telemetry->SetSelectedStorageDrives(driveLetters);
    state_.telemetryUpdate = state_.telemetry->Latest();
    ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
    FinishConfigMutation(shell);
}

void DashboardController::RefreshTelemetrySelections(DashboardShellHost& shell) {
    if (state_.telemetry == nullptr) {
        return;
    }
    state_.telemetry->RefreshSelections();
    state_.telemetryUpdate = state_.telemetry->Latest();
    ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
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
    return state_.isEditingLayout && state_.layoutEditSessionSavedLayout != nullptr &&
           state_.hasUnsavedLayoutEditChanges &&
           LayoutConfigHasDifferences(state_.config.layout, *state_.layoutEditSessionSavedLayout);
}

bool DashboardController::RestoreLayoutEditSessionSavedLayout(DashboardShellHost& shell) {
    if (state_.layoutEditSessionSavedLayout == nullptr) {
        return false;
    }

    state_.config.layout = *state_.layoutEditSessionSavedLayout;
    if (!SelectResolvedLayout(state_.config, state_.config.display.layout)) {
        return false;
    }
    if (state_.telemetry != nullptr) {
        state_.telemetry->SetPreferredNetworkAdapterName(state_.config.network.adapterName);
        state_.telemetry->SetSelectedStorageDrives(state_.config.storage.drives);
        state_.telemetry->RefreshSelections();
        state_.telemetryUpdate = state_.telemetry->Latest();
        ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
    }
    FinishConfigMutation(shell);
    MarkLayoutEditSessionSaved();
    return true;
}

bool DashboardController::ApplyLayoutGuideWeights(
    DashboardShellHost& shell, const LayoutEditLayoutTarget& target, const std::vector<int>& weights) {
    if (!ApplyGuideWeights(state_.config, target, weights)) {
        return false;
    }
    // Perf: layout-only drag mutations do not affect the themed shell icon; keep icon rendering off the pointer path.
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyLayoutGuideAdjacentWeights(DashboardShellHost& shell,
    const LayoutEditLayoutTarget& target,
    size_t separatorIndex,
    int firstWeight,
    int secondWeight) {
    if (!ApplyGuideAdjacentWeights(state_.config, target, separatorIndex, firstWeight, secondWeight)) {
        return false;
    }
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyMetricListOrder(
    DashboardShellHost& shell, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) {
    if (!::ApplyMetricListOrder(state_.config, widget, metricRefs)) {
        return false;
    }
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyContainerChildOrder(
    DashboardShellHost& shell, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) {
    if (!::ApplyContainerChildOrder(state_.config, key, fromIndex, toIndex)) {
        return false;
    }
    return FinishConfigMutation(shell, false);
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
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyLayoutEditFont(
    DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, const UiFontConfig& value) {
    if (!ApplyLayoutEditParameterFontValue(state_.config, parameter, value)) {
        return false;
    }
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyLayoutEditFontFamily(DashboardShellHost& shell, const std::string& family) {
    AssignCommonFontFamily(state_.config.layout.fonts, family);
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyLayoutEditFontSet(DashboardShellHost& shell, const UiFontSetConfig& fonts) {
    state_.config.layout.fonts = fonts;
    return FinishConfigMutation(shell, false);
}

bool DashboardController::ApplyLayoutEditColor(
    DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, unsigned int value) {
    if (!ApplyLayoutEditParameterColorValue(state_.config, parameter, value)) {
        return false;
    }
    ResolveConfiguredColors(state_.config);
    return FinishConfigMutation(shell);
}

bool DashboardController::ApplyLayoutEditColorExpression(
    DashboardShellHost& shell, DashboardRenderer::LayoutEditParameter parameter, const std::string& expression) {
    ColorConfig* target = FindLayoutColorConfig(state_.config.layout.colors, parameter);
    if (target == nullptr) {
        return false;
    }
    target->expression = expression;
    ResolveConfiguredColors(state_.config);
    return FinishConfigMutation(shell);
}

bool DashboardController::ApplyLayoutEditTheme(DashboardShellHost& shell, const std::string& themeName) {
    if (FindThemeConfig(state_.config.layout, themeName) == nullptr) {
        return false;
    }
    state_.config.display.theme = themeName;
    ResolveConfiguredColors(state_.config);
    return FinishConfigMutation(shell);
}

bool DashboardController::ApplyLayoutEditThemeColor(
    DashboardShellHost& shell, const ThemeColorEditKey& key, unsigned int value) {
    ThemeConfig* theme = FindThemeConfig(state_.config.layout, key.themeName);
    if (theme == nullptr) {
        return false;
    }
    ColorConfig* target = FindThemeColorConfig(*theme, key.tokenName);
    if (target == nullptr) {
        return false;
    }
    *target = ColorConfig::FromRgba(value);
    ResolveConfiguredColors(state_.config);
    return FinishConfigMutation(shell);
}

bool DashboardController::ApplyLayoutEditCardTitle(
    DashboardShellHost& shell, const LayoutCardTitleEditKey& key, const std::string& title) {
    LayoutCardConfig* card = FindCardConfig(state_.config.layout, key.cardId);
    if (card == nullptr) {
        return false;
    }
    card->title = title;
    return FinishConfigMutation(shell);
}

void DashboardController::ApplyConfigSnapshot(DashboardShellHost& shell, const AppConfig& config) {
    const TelemetrySettings previousSettings = ExtractTelemetrySettings(state_.config);
    state_.config = config;
    if (state_.telemetry != nullptr) {
        const TelemetrySettings nextSettings = ExtractTelemetrySettings(state_.config);
        if (previousSettings != nextSettings) {
            state_.telemetry->Reconfigure(nextSettings);
            state_.telemetryUpdate = state_.telemetry->Latest();
            ApplyResolvedTelemetrySelections(state_.config, state_.telemetryUpdate.resolvedSelections);
        }
    }
    FinishConfigMutation(shell);
}

std::optional<int> DashboardController::EvaluateLayoutWidgetExtentForWeights(DashboardShellHost& shell,
    const LayoutEditLayoutTarget& target,
    const std::vector<int>& weights,
    const LayoutEditWidgetIdentity& widget,
    LayoutGuideAxis axis) {
    DashboardRenderer& renderer = shell.Renderer();
    if (!renderer.ApplyLayoutGuideWeightsPreview(target.editCardId, target.nodePath, weights)) {
        return std::nullopt;
    }
    return renderer.FindLayoutWidgetExtent(widget, axis);
}

AppConfig DashboardController::BuildCurrentConfigForSaving(DashboardShellHost& shell) const {
    AppConfig config = state_.config;
    if (state_.telemetry != nullptr) {
        ApplyResolvedTelemetrySelections(config, state_.telemetryUpdate.resolvedSelections);
    }
    const MonitorPlacementInfo placement = shell.GetWindowPlacementInfo();
    config.display.monitorName =
        !placement.configMonitorName.empty() ? placement.configMonitorName : placement.deviceName;
    config.display.position.x = placement.relativePosition.x;
    config.display.position.y = placement.relativePosition.y;
    return config;
}

bool DashboardController::UpdateConfigFromCurrentPlacement(DashboardShellHost& shell) {
    const FilePath configPath = GetRuntimeConfigPath();
    AppConfig config = BuildCurrentConfigForSaving(shell);
    if (!SaveRuntimeConfig(configPath, config, shell.WindowHandle())) {
        shell.ShowError("Failed to save " + configPath.string() + ".");
        return false;
    }
    state_.config = std::move(config);
    SyncRenderer(shell, state_.isEditingLayout);
    if (state_.isEditingLayout) {
        MarkLayoutEditSessionSaved();
    }
    return true;
}
