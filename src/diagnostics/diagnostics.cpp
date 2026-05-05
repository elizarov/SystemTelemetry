#include "diagnostics/diagnostics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <vector>

#include "config/color_format.h"
#include "config/color_resolver.h"
#include "config/config_io.h"
#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "config/config_writer.h"
#include "diagnostics/app_icon_export.h"
#include "diagnostics/constants.h"
#include "layout_edit/layout_edit_active_region_trace.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_tooltip_text.h"
#include "layout_guide_sheet/layout_guide_sheet.h"
#include "telemetry/metrics.h"
#include "telemetry/telemetry.h"
#include "util/command_line.h"
#include "util/paths.h"
#include "util/scale.h"
#include "util/strings.h"
#include "util/utf8.h"
#include "widget/app_icon_geometry.h"

namespace {

std::optional<int> TryParseInteger(std::wstring_view text) {
    while (!text.empty() && iswspace(text.front()) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && iswspace(text.back()) != 0) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    wchar_t* end = nullptr;
    std::wstring owned(text);
    const long value = std::wcstol(owned.c_str(), &end, 10);
    if (end == owned.c_str() || end == nullptr || *end != L'\0' || value < (std::numeric_limits<int>::min)() ||
        value > (std::numeric_limits<int>::max)()) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<DiagnosticsHoverPoint> TryParseHoverPointValue(const std::wstring& text) {
    const size_t comma = text.find(L',');
    if (comma == std::wstring::npos || text.find(L',', comma + 1) != std::wstring::npos) {
        return std::nullopt;
    }
    const auto x = TryParseInteger(std::wstring_view(text).substr(0, comma));
    const auto y = TryParseInteger(std::wstring_view(text).substr(comma + 1));
    if (!x.has_value() || !y.has_value()) {
        return std::nullopt;
    }
    return DiagnosticsHoverPoint{*x, *y};
}

void WriteResolvedColorTraceLine(
    DiagnosticsSession& diagnostics, std::string_view section, std::string_view name, const ColorConfig& color) {
    std::string text = "diagnostics:resolved_color section=" + Trace::QuoteText(section) +
                       " name=" + Trace::QuoteText(name) +
                       " value=" + Trace::QuoteText(FormatRgbaColorText(color.ToRgba()));
    if (!color.expression.empty()) {
        text += " expression=" + Trace::QuoteText(color.expression);
    }
    diagnostics.WriteTraceMarker(text);
}

void WriteResolvedColorTrace(DiagnosticsSession& diagnostics, const AppConfig& config) {
    struct ColorsTraceField {
        const char* name;
        const ColorConfig ColorsConfig::* color;
    };

    static constexpr ColorsTraceField kColorFields[] = {
        {"background_color", &ColorsConfig::backgroundColor},
        {"foreground_color", &ColorsConfig::foregroundColor},
        {"icon_color", &ColorsConfig::iconColor},
        {"accent_color", &ColorsConfig::accentColor},
        {"peak_ghost_color", &ColorsConfig::peakGhostColor},
        {"warning_color", &ColorsConfig::warningColor},
        {"layout_guide_color", &ColorsConfig::layoutGuideColor},
        {"active_edit_color", &ColorsConfig::activeEditColor},
        {"panel_border_color", &ColorsConfig::panelBorderColor},
        {"muted_text_color", &ColorsConfig::mutedTextColor},
        {"track_color", &ColorsConfig::trackColor},
        {"panel_fill_color", &ColorsConfig::panelFillColor},
        {"graph_background_color", &ColorsConfig::graphBackgroundColor},
        {"graph_axis_color", &ColorsConfig::graphAxisColor},
        {"graph_marker_color", &ColorsConfig::graphMarkerColor},
    };

    struct LayoutGuideSheetTraceField {
        const char* name;
        const ColorConfig LayoutGuideSheetConfig::* color;
    };

    static constexpr LayoutGuideSheetTraceField kSheetColorFields[] = {
        {"callout_leader_color", &LayoutGuideSheetConfig::calloutLeaderColor},
        {"callout_fill_color", &LayoutGuideSheetConfig::calloutFillColor},
        {"callout_border_color", &LayoutGuideSheetConfig::calloutBorderColor},
        {"callout_parameter_color", &LayoutGuideSheetConfig::calloutParameterColor},
        {"callout_description_color", &LayoutGuideSheetConfig::calloutDescriptionColor},
    };

    const ColorsConfig& colors = config.layout.colors;
    for (const ColorsTraceField& field : kColorFields) {
        WriteResolvedColorTraceLine(diagnostics, "colors", field.name, colors.*field.color);
    }

    const LayoutGuideSheetConfig& sheet = config.layout.layoutGuideSheet;
    for (const LayoutGuideSheetTraceField& field : kSheetColorFields) {
        WriteResolvedColorTraceLine(diagnostics, "layout_guide_sheet", field.name, sheet.*field.color);
    }
}

class DiagnosticsLayoutEditHost final : public LayoutEditHost {
public:
    DiagnosticsLayoutEditHost(const AppConfig& config, DashboardRenderer& renderer, DashboardOverlayState& overlayState)
        : config_(config), renderer_(renderer), overlayState_(overlayState) {}

    const AppConfig& LayoutEditConfig() const override {
        return config_;
    }

    DashboardOverlayState& LayoutDashboardOverlayState() override {
        return overlayState_;
    }

    LayoutEditActiveRegions CollectLayoutEditActiveRegions() const override {
        return renderer_.CollectLayoutEditActiveRegions(overlayState_);
    }

    double LayoutEditRenderScale() const override {
        return renderer_.RenderScale();
    }

    int LayoutEditSimilarityThreshold() const override {
        return renderer_.LayoutSimilarityThreshold();
    }

    void SetLayoutGuideDragActive(bool active) override {
        renderer_.SetLayoutGuideDragActive(active);
    }

    void SetLayoutEditInteractiveDragTraceActive(bool active) override {
        renderer_.SetInteractiveDragTraceActive(active);
    }

    void RebuildLayoutEditArtifacts() override {
        renderer_.RebuildEditArtifacts();
    }

    bool ApplyLayoutGuideWeights(const LayoutEditLayoutTarget& target, const std::vector<int>& weights) override {
        (void)target;
        (void)weights;
        return false;
    }

    bool ApplyMetricListOrder(
        const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) override {
        (void)widget;
        (void)metricRefs;
        return false;
    }

    bool ApplyContainerChildOrder(const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) override {
        (void)key;
        (void)fromIndex;
        (void)toIndex;
        return false;
    }

    std::optional<int> EvaluateLayoutWidgetExtentForWeights(const LayoutEditLayoutTarget& target,
        const std::vector<int>& weights,
        const LayoutEditWidgetIdentity& widget,
        LayoutGuideAxis axis) override {
        (void)target;
        (void)weights;
        (void)widget;
        (void)axis;
        return std::nullopt;
    }

    bool ApplyLayoutEditValue(LayoutEditParameter parameter, double value) override {
        (void)parameter;
        (void)value;
        return false;
    }

    void InvalidateLayoutEdit() override {}

    void BeginLayoutEditTraceSession(const std::string& kind, const std::string& detail) override {
        (void)kind;
        (void)detail;
    }

    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override {
        (void)phase;
        (void)elapsed;
    }

    void EndLayoutEditTraceSession(const std::string& reason) override {
        (void)reason;
    }

private:
    const AppConfig& config_;
    DashboardRenderer& renderer_;
    DashboardOverlayState& overlayState_;
};

}  // namespace

std::optional<double> TryParseScaleValue(const std::wstring& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::wstring normalized(text);
    std::replace(normalized.begin(), normalized.end(), L',', L'.');
    wchar_t* end = nullptr;
    const double value = std::wcstod(normalized.c_str(), &end);
    if (end == normalized.c_str() || end == nullptr || *end != L'\0' || !std::isfinite(value) || value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> GetScaleSwitchValue() {
    if (const auto value = GetColonSwitchValue(L"/scale"); value.has_value()) {
        return TryParseScaleValue(*value);
    }
    return std::nullopt;
}

std::optional<int> TryParseAppIconSizeValue(const std::wstring& text) {
    const std::optional<int> value = TryParseInteger(text);
    if (!value.has_value() || !IsValidAppIconSize(*value)) {
        return std::nullopt;
    }
    return *value;
}

std::optional<std::string> GetLayoutSwitchValue() {
    if (const auto value = GetColonSwitchValue(L"/layout"); value.has_value()) {
        const std::string layoutName = Trim(Utf8FromWide(*value));
        if (!layoutName.empty()) {
            return layoutName;
        }
    }
    return std::nullopt;
}

std::optional<std::string> GetThemeSwitchValue() {
    if (const auto value = GetColonSwitchValue(L"/theme"); value.has_value()) {
        const std::string themeName = Trim(Utf8FromWide(*value));
        if (!themeName.empty()) {
            return themeName;
        }
    }
    return std::nullopt;
}

void WriteValidationFailureTrace(
    const DiagnosticsOptions& options, const std::string& reason, const std::string& message) {
    if (!options.trace) {
        return;
    }

    const FilePath tracePath =
        ResolveDiagnosticsOutputPath(GetWorkingDirectory(), options.tracePath, kDefaultTraceFileName);
    std::FILE* traceFile = nullptr;
    if (_wfopen_s(&traceFile, tracePath.c_str(), L"ab") != 0 || traceFile == nullptr) {
        return;
    }

    Trace trace(traceFile);
    trace.Write(
        "diagnostics:validation_failed reason=" + Trace::QuoteText(reason) + " message=" + Trace::QuoteText(message));
    fclose(traceFile);
}

DashboardRenderer::RenderMode GetDiagnosticsRenderMode(const DiagnosticsOptions& options) {
    return options.blank ? DashboardRenderer::RenderMode::Blank : DashboardRenderer::RenderMode::Normal;
}

LayoutSimilarityIndicatorMode GetSimilarityIndicatorMode(const DiagnosticsOptions& options) {
    switch (options.layoutSimilarityMode) {
        case DiagnosticsLayoutSimilarityMode::HorizontalSizes:
            return LayoutSimilarityIndicatorMode::AllHorizontal;
        case DiagnosticsLayoutSimilarityMode::VerticalSizes:
            return LayoutSimilarityIndicatorMode::AllVertical;
        case DiagnosticsLayoutSimilarityMode::None:
        default:
            return LayoutSimilarityIndicatorMode::ActiveGuide;
    }
}

DiagnosticsOptions GetDiagnosticsOptions() {
    DiagnosticsOptions options;
    options.trace = HasSwitch("/trace");
    options.dump = HasSwitch("/dump");
    options.screenshot = HasSwitch("/screenshot");
    options.layoutGuideSheet = HasSwitch("/layout-guide-sheet");
    options.appIcon = HasSwitch("/app-icon");
    options.exit = HasSwitch("/exit");
    options.fake = HasSwitch("/fake");
    options.blank = HasSwitch("/blank");
    options.editLayout = HasSwitch("/edit-layout");
    options.reload = HasSwitch("/reload");
    options.defaultConfig = HasSwitch("/default-config");
    if (const auto editLayoutValue = GetColonSwitchValue(L"/edit-layout"); editLayoutValue.has_value()) {
        const std::string mode = ToLower(Trim(Utf8FromWide(*editLayoutValue)));
        options.editLayout = true;
        if (mode == "horizontal-sizes" || mode == "horizonatal-sizes") {
            options.layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::HorizontalSizes;
        } else if (mode == "vertical-sizes") {
            options.layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::VerticalSizes;
        } else if (!mode.empty()) {
            options.editLayoutWidgetName = mode;
        }
    }
    if (const auto layoutName = GetLayoutSwitchValue(); layoutName.has_value()) {
        options.layoutName = *layoutName;
    }
    if (const auto themeName = GetThemeSwitchValue(); themeName.has_value()) {
        options.themeName = *themeName;
    }
    if (const auto scale = GetScaleSwitchValue(); scale.has_value()) {
        options.hasScaleOverride = true;
        options.scale = *scale;
    }
    if (const auto appIconSizeValue = GetColonSwitchValue(L"/app-icon-size"); appIconSizeValue.has_value()) {
        options.hasAppIconSize = true;
        if (const auto appIconSize = TryParseAppIconSizeValue(*appIconSizeValue); appIconSize.has_value()) {
            options.appIconSize = *appIconSize;
        } else {
            options.appIconSize = 0;
        }
    }
    if (const auto hoverValue = GetColonSwitchValue(L"/hover"); hoverValue.has_value()) {
        if (const auto hoverPoint = TryParseHoverPointValue(*hoverValue); hoverPoint.has_value()) {
            options.hoverPoint = *hoverPoint;
            options.editLayout = true;
        }
    }
    if (const auto tracePath = GetColonSwitchValue(L"/trace"); tracePath.has_value()) {
        options.trace = true;
        options.tracePath = *tracePath;
    }
    if (const auto dumpPath = GetColonSwitchValue(L"/dump"); dumpPath.has_value()) {
        options.dump = true;
        options.dumpPath = *dumpPath;
    }
    if (const auto screenshotPath = GetColonSwitchValue(L"/screenshot"); screenshotPath.has_value()) {
        options.screenshot = true;
        options.screenshotPath = *screenshotPath;
    }
    if (const auto layoutGuideSheetPath = GetColonSwitchValue(L"/layout-guide-sheet");
        layoutGuideSheetPath.has_value()) {
        options.layoutGuideSheet = true;
        options.layoutGuideSheetPath = *layoutGuideSheetPath;
    }
    if (const auto appIconPath = GetColonSwitchValue(L"/app-icon"); appIconPath.has_value()) {
        options.appIcon = true;
        options.appIconPath = *appIconPath;
    }
    if (const auto saveConfigPath = GetColonSwitchValue(L"/save-config"); saveConfigPath.has_value()) {
        options.saveConfig = true;
        options.saveConfigPath = *saveConfigPath;
    } else if (HasSwitch("/save-config")) {
        options.saveConfig = true;
    }
    if (const auto saveFullConfigPath = GetColonSwitchValue(L"/save-full-config"); saveFullConfigPath.has_value()) {
        options.saveFullConfig = true;
        options.saveFullConfigPath = *saveFullConfigPath;
    } else if (HasSwitch("/save-full-config")) {
        options.saveFullConfig = true;
    }
    if (const auto fakePath = GetColonSwitchValue(L"/fake"); fakePath.has_value()) {
        options.fake = true;
        options.fakePath = *fakePath;
    }
    return options;
}

bool ValidateDiagnosticsOptions(const DiagnosticsOptions& options) {
    if (options.blank && options.fake) {
        if (!options.trace) {
            MessageBoxW(nullptr, L"/blank cannot be used together with /fake.", L"CaseDash", MB_ICONERROR);
        }
        WriteValidationFailureTrace(options, "blank_fake_conflict", "/blank cannot be used together with /fake.");
        return false;
    }
    if (options.blank && options.layoutGuideSheet) {
        if (!options.trace) {
            MessageBoxW(
                nullptr, L"/blank cannot be used together with /layout-guide-sheet.", L"CaseDash", MB_ICONERROR);
        }
        WriteValidationFailureTrace(
            options, "blank_layout_guide_sheet_conflict", "/blank cannot be used together with /layout-guide-sheet.");
        return false;
    }
    if (options.hasAppIconSize && !IsValidAppIconSize(options.appIconSize)) {
        if (!options.trace) {
            MessageBoxW(nullptr, L"/app-icon-size must be between 16 and 1024 pixels.", L"CaseDash", MB_ICONERROR);
        }
        WriteValidationFailureTrace(options, "app_icon_size", "/app-icon-size must be between 16 and 1024 pixels.");
        return false;
    }
    return true;
}

bool ApplyDiagnosticsLayoutOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics) {
    if (options.layoutName.empty()) {
        return true;
    }
    if (SelectLayout(config, options.layoutName)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:layout_override name=\"" + options.layoutName + "\"");
        }
        return true;
    }

    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:layout_override_failed name=\"" + options.layoutName + "\"");
        return false;
    }

    const std::wstring message = WideFromUtf8("Unknown layout name:\n" + options.layoutName);
    MessageBoxW(nullptr, message.c_str(), L"CaseDash", MB_ICONERROR);
    return false;
}

bool ApplyDiagnosticsThemeOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics) {
    if (options.themeName.empty()) {
        return true;
    }
    const auto themeIt = std::find_if(config.layout.themes.begin(),
        config.layout.themes.end(),
        [&](const ThemeConfig& theme) { return theme.name == options.themeName; });
    if (themeIt != config.layout.themes.end()) {
        config.display.theme = options.themeName;
        ResolveConfiguredColors(config);
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:theme_override name=\"" + options.themeName + "\"");
        }
        return true;
    }

    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:theme_override_failed name=\"" + options.themeName + "\"");
        return false;
    }

    const std::wstring message = WideFromUtf8("Unknown theme name:\n" + options.themeName);
    MessageBoxW(nullptr, message.c_str(), L"CaseDash", MB_ICONERROR);
    return false;
}

double ResolveSavedScreenshotScale(const AppConfig& config) {
    return HasExplicitDisplayScale(config.display.scale) ? config.display.scale : 1.0;
}

DiagnosticsSession::DiagnosticsSession(const DiagnosticsOptions& options, Trace& trace)
    : options_(options), trace_(trace) {}

DiagnosticsSession::~DiagnosticsSession() {
    if (traceFile_ != nullptr) {
        trace_.SetOutput(nullptr);
        fclose(traceFile_);
    }
}

bool DiagnosticsSession::Initialize() {
    const FilePath workingDirectory = GetWorkingDirectory();
    if (options_.trace) {
        tracePath_ = ResolveDiagnosticsOutputPath(workingDirectory, options_.tracePath, kDefaultTraceFileName);
        if (_wfopen_s(&traceFile_, tracePath_.c_str(), L"ab") != 0 || traceFile_ == nullptr) {
            ShowFileOpenError("trace file", tracePath_);
            return false;
        }
        trace_.SetOutput(traceFile_);
    }
    if (options_.dump) {
        dumpPath_ = ResolveDiagnosticsOutputPath(workingDirectory, options_.dumpPath, kDefaultDumpFileName);
    }
    if (options_.screenshot) {
        screenshotPath_ =
            ResolveDiagnosticsOutputPath(workingDirectory, options_.screenshotPath, kDefaultScreenshotFileName);
    }
    if (options_.layoutGuideSheet) {
        layoutGuideSheetPath_ = ResolveDiagnosticsOutputPath(
            workingDirectory, options_.layoutGuideSheetPath, kDefaultLayoutGuideSheetFileName);
    }
    if (options_.appIcon) {
        appIconPath_ = ResolveDiagnosticsOutputPath(workingDirectory, options_.appIconPath, kDefaultAppIconFileName);
    }
    if (options_.saveConfig) {
        saveConfigPath_ =
            ResolveDiagnosticsOutputPath(workingDirectory, options_.saveConfigPath, kDefaultSavedConfigFileName);
    }
    if (options_.saveFullConfig) {
        saveFullConfigPath_ = ResolveDiagnosticsOutputPath(
            workingDirectory, options_.saveFullConfigPath, kDefaultSavedFullConfigFileName);
    }
    return true;
}

bool DiagnosticsSession::ShouldShowDialogs() const {
    return !options_.trace;
}

void DiagnosticsSession::WriteTraceMarker(const std::string& text) {
    trace_.Write(text);
}

void DiagnosticsSession::ReportError(const std::string& traceText, const std::wstring& message) {
    WriteTraceMarker(traceText);
    if (ShouldShowDialogs()) {
        MessageBoxW(nullptr, message.c_str(), L"CaseDash", MB_ICONERROR);
    }
}

bool DiagnosticsSession::WriteOutputs(const TelemetryDump& dump, const AppConfig& config) {
    if (options_.dump) {
        std::FILE* dumpFile = nullptr;
        if (_wfopen_s(&dumpFile, dumpPath_.c_str(), L"wb") != 0 || dumpFile == nullptr) {
            ShowFileOpenError("dump file", dumpPath_);
            return false;
        }
        const bool dumpWritten = WriteTelemetryDump(dumpFile, dump);
        fclose(dumpFile);
        if (!dumpWritten) {
            const std::string pathText = Utf8FromWide(dumpPath_.wstring());
            const std::wstring message = WideFromUtf8("Failed to write dump file:\n" + pathText);
            ReportError("diagnostics:dump_write_failed path=\"" + pathText + "\"", message);
            return false;
        }
    }

    std::string screenshotError;
    if (options_.screenshot &&
        !SaveDumpScreenshot(screenshotPath_,
            dump.snapshot,
            config,
            ResolveSavedScreenshotScale(config),
            GetDiagnosticsRenderMode(options_),
            options_.editLayout,
            GetSimilarityIndicatorMode(options_),
            options_.editLayoutWidgetName,
            trace_,
            options_.hoverPoint.has_value()
                ? std::optional<RenderPoint>(RenderPoint{options_.hoverPoint->x, options_.hoverPoint->y})
                : std::nullopt,
            &screenshotError)) {
        const std::string pathText = Utf8FromWide(screenshotPath_.wstring());
        const std::wstring message = WideFromUtf8("Failed to save screenshot:\n" + pathText);
        std::string traceText = "diagnostics:screenshot_save_failed path=\"" + pathText + "\"";
        if (!screenshotError.empty()) {
            traceText += " detail=\"" + screenshotError + "\"";
        }
        ReportError(traceText, message);
        return false;
    }

    std::string layoutGuideSheetError;
    if (options_.layoutGuideSheet && !SaveLayoutGuideSheet(layoutGuideSheetPath_,
                                         dump.snapshot,
                                         config,
                                         ResolveSavedScreenshotScale(config),
                                         trace_,
                                         &layoutGuideSheetError)) {
        const std::string pathText = Utf8FromWide(layoutGuideSheetPath_.wstring());
        const std::wstring message = WideFromUtf8("Failed to save layout guide sheet:\n" + pathText);
        std::string traceText = "diagnostics:layout_guide_sheet_save_failed path=\"" + pathText + "\"";
        if (!layoutGuideSheetError.empty()) {
            traceText += " detail=\"" + layoutGuideSheetError + "\"";
        }
        ReportError(traceText, message);
        return false;
    }

    std::string appIconError;
    if (options_.appIcon && !SaveRenderedAppIcon(appIconPath_, config, options_.appIconSize, &appIconError)) {
        const std::string pathText = Utf8FromWide(appIconPath_.wstring());
        const std::wstring message = WideFromUtf8("Failed to save app icon:\n" + pathText);
        std::string traceText =
            "diagnostics:app_icon_save_failed path=\"" + pathText + "\" size=" + std::to_string(options_.appIconSize);
        if (!appIconError.empty()) {
            traceText += " detail=\"" + appIconError + "\"";
        }
        ReportError(traceText, message);
        return false;
    }
    if (options_.appIcon) {
        const std::string pathText = Utf8FromWide(appIconPath_.wstring());
        WriteTraceMarker(
            "diagnostics:app_icon_saved path=\"" + pathText + "\" size=" + std::to_string(options_.appIconSize));
    }

    if (options_.saveConfig && !SaveConfig(saveConfigPath_, config, ConfigParseContext{TelemetryMetricCatalog()})) {
        const std::string pathText = Utf8FromWide(saveConfigPath_.wstring());
        const std::wstring message = WideFromUtf8("Failed to save config file:\n" + pathText);
        ReportError("diagnostics:config_save_failed path=\"" + pathText + "\"", message);
        return false;
    }

    if (options_.saveFullConfig && !SaveFullConfig(saveFullConfigPath_, config)) {
        const std::string pathText = Utf8FromWide(saveFullConfigPath_.wstring());
        const std::wstring message = WideFromUtf8("Failed to save full config file:\n" + pathText);
        ReportError("diagnostics:full_config_save_failed path=\"" + pathText + "\"", message);
        return false;
    }

    return true;
}

void DiagnosticsSession::ShowFileOpenError(const char* label, const FilePath& path) {
    const std::string pathText = Utf8FromWide(path.wstring());
    const std::wstring message = WideFromUtf8(std::string("Failed to open ") + label + ":\n" + pathText);
    ReportError("diagnostics:file_open_failed label=\"" + std::string(label) + "\" path=\"" + pathText + "\"", message);
}

FilePath ResolveDiagnosticsOutputPath(
    const FilePath& workingDirectory, const FilePath& configuredPath, const wchar_t* defaultFileName) {
    if (configuredPath.empty()) {
        return workingDirectory / defaultFileName;
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return workingDirectory / configuredPath;
}

std::optional<FilePath> PromptSavePath(HWND owner,
    const FilePath& initialDirectory,
    const wchar_t* defaultFileName,
    const wchar_t* filter,
    const wchar_t* defaultExtension) {
    wchar_t fileBuffer[MAX_PATH] = {};
    wcsncpy_s(fileBuffer, defaultFileName != nullptr ? defaultFileName : L"", _TRUNCATE);

    std::wstring initialDirectoryText = initialDirectory.wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = fileBuffer;
    dialog.nMaxFile = ARRAYSIZE(fileBuffer);
    dialog.lpstrInitialDir = initialDirectoryText.empty() ? nullptr : initialDirectoryText.c_str();
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&dialog)) {
        return std::nullopt;
    }
    return FilePath(dialog.lpstrFile);
}

TelemetryCollectorOptions BuildTelemetryCollectorOptions(const DiagnosticsOptions& diagnosticsOptions) {
    TelemetryCollectorOptions options;
    options.fake = diagnosticsOptions.fake;
    options.fakePath = diagnosticsOptions.fakePath;
    options.loadFakeDump = &LoadTelemetryDump;
    return options;
}

int RunElevatedSaveConfigMode(const FilePath& sourcePath, const FilePath& targetPath) {
    if (sourcePath.empty() || targetPath.empty()) {
        return 2;
    }

    const AppConfig config = LoadConfig(sourcePath, true, ConfigParseContext{TelemetryMetricCatalog()});
    if (!SaveConfig(targetPath, config, ConfigParseContext{TelemetryMetricCatalog()})) {
        return 1;
    }
    RemoveFileIfExists(sourcePath);
    return 0;
}

std::wstring FormatTelemetryInitializeError(std::string_view errorText) {
    std::string message = "Failed to initialize telemetry collector.";
    if (!errorText.empty()) {
        message += "\n\n";
        message += errorText;
    }
    return WideFromUtf8(message);
}

std::unique_ptr<TelemetryRuntime> InitializeTelemetryRuntimeInstance(const AppConfig& runtimeConfig,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    TelemetryUpdateSink* callback,
    std::string* errorText) {
    if (errorText != nullptr) {
        errorText->clear();
    }
    return CreateTelemetryRuntime(BuildTelemetryCollectorOptions(diagnosticsOptions),
        GetWorkingDirectory(),
        ExtractTelemetrySettings(runtimeConfig),
        trace,
        callback,
        errorText);
}

bool ReloadTelemetryCollectorFromDisk(const FilePath& configPath,
    AppConfig& activeConfig,
    std::unique_ptr<TelemetryRuntime>& telemetry,
    const DiagnosticsOptions& diagnosticsOptions,
    Trace& trace,
    DiagnosticsSession* diagnostics,
    TelemetryUpdateSink* callback,
    std::string* errorText) {
    if (errorText != nullptr) {
        errorText->clear();
    }
    const AppConfig reloadedConfig =
        LoadConfig(configPath, !diagnosticsOptions.defaultConfig, ConfigParseContext{TelemetryMetricCatalog()});
    AppConfig effectiveReloadedConfig = reloadedConfig;
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:reload_config_begin");
    }
    if (!ApplyDiagnosticsLayoutOverride(effectiveReloadedConfig, diagnosticsOptions, diagnostics)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    if (!ApplyDiagnosticsThemeOverride(effectiveReloadedConfig, diagnosticsOptions, diagnostics)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker("diagnostics:reload_config_failed");
        }
        return false;
    }
    ApplyDiagnosticsScaleOverride(effectiveReloadedConfig, diagnosticsOptions);

    telemetry.reset();
    std::string reloadError;
    std::unique_ptr<TelemetryRuntime> reloadedTelemetry =
        InitializeTelemetryRuntimeInstance(effectiveReloadedConfig, diagnosticsOptions, trace, callback, &reloadError);
    if (reloadedTelemetry == nullptr) {
        telemetry = InitializeTelemetryRuntimeInstance(activeConfig, diagnosticsOptions, trace, callback);
        if (errorText != nullptr) {
            *errorText = reloadError;
        }
        if (diagnostics != nullptr) {
            std::string traceText = "diagnostics:reload_config_failed";
            if (!reloadError.empty()) {
                traceText += " detail=" + Trace::QuoteText(reloadError);
            }
            diagnostics->WriteTraceMarker(traceText);
        }
        return false;
    }

    telemetry = std::move(reloadedTelemetry);
    const TelemetryUpdate reloadedUpdate = telemetry->Latest();
    activeConfig = std::move(effectiveReloadedConfig);
    ApplyResolvedTelemetrySelections(activeConfig, reloadedUpdate.resolvedSelections);
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker("diagnostics:reload_config_done");
        WriteResolvedColorTrace(*diagnostics, activeConfig);
    }
    return true;
}

bool SaveDumpScreenshot(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    DashboardRenderer::RenderMode renderMode,
    bool showLayoutEditGuides,
    LayoutSimilarityIndicatorMode similarityIndicatorMode,
    const std::string& editLayoutWidgetName,
    Trace& trace,
    std::optional<RenderPoint> hoverPoint,
    std::string* errorText) {
    DashboardRenderer renderer(trace);
    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = showLayoutEditGuides || hoverPoint.has_value();
    overlayState.forceLayoutEditAffordances = showLayoutEditGuides && !hoverPoint.has_value();
    overlayState.similarityIndicatorMode = similarityIndicatorMode;
    renderer.SetRenderScale(scale);
    renderer.SetConfig(config);
    renderer.SetRenderMode(renderMode);
    if (!renderer.Initialize()) {
        if (errorText != nullptr) {
            *errorText = renderer.LastError();
        }
        return false;
    }
    if (!editLayoutWidgetName.empty()) {
        const auto widget = renderer.FindFirstLayoutEditPreviewWidget(editLayoutWidgetName);
        if (!widget.has_value()) {
            if (errorText != nullptr) {
                *errorText = "renderer:edit_layout_widget_not_found name=\"" + editLayoutWidgetName + "\"";
            }
            return false;
        }
        overlayState.SetPreviewWidget(*widget);
        trace.Write("diagnostics:edit_layout_widget name=\"" + editLayoutWidgetName + "\"");
    }
    if (hoverPoint.has_value()) {
        if (!renderer.PrimeLayoutEditDynamicRegions(snapshot, overlayState)) {
            if (errorText != nullptr) {
                *errorText = renderer.LastError();
            }
            return false;
        }

        DiagnosticsLayoutEditHost host(config, renderer, overlayState);
        LayoutEditController controller(host);
        controller.HandleMouseMove(*hoverPoint);
        if (const auto target = controller.CurrentTooltipTarget(); target.has_value()) {
            std::string tooltipError;
            const auto tooltipText = BuildLayoutEditTooltipTextForPayload(config, target->payload, &tooltipError);
            std::string traceText =
                "diagnostics:hover point=" + Trace::QuoteText(Trace::FormatPoint(hoverPoint->x, hoverPoint->y)) +
                " target=" + Trace::QuoteText(LayoutEditTooltipPayloadTraceKind(target->payload));
            if (tooltipText.has_value()) {
                traceText += " tooltip=" + Trace::QuoteText(Utf8FromWide(*tooltipText));
            } else {
                traceText +=
                    " tooltip_error=" + Trace::QuoteText(tooltipError.empty() ? "unsupported_target" : tooltipError);
            }
            trace.Write(traceText);
        } else {
            trace.Write(
                "diagnostics:hover point=" + Trace::QuoteText(Trace::FormatPoint(hoverPoint->x, hoverPoint->y)) +
                " target=" + Trace::QuoteText("none"));
        }
    }
    const bool saved = renderer.SaveSnapshotPng(imagePath, snapshot, overlayState);
    if (!saved && errorText != nullptr) {
        *errorText = renderer.LastError();
    }
    if (saved) {
        WriteLayoutEditActiveRegionTrace(
            trace, config, renderer.CollectLayoutEditActiveRegions(overlayState), overlayState);
    }
    return saved;
}

bool SaveLayoutGuideSheet(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText) {
    return SaveLayoutGuideSheetPng(imagePath, snapshot, config, scale, trace, errorText);
}

bool SaveRenderedAppIcon(const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText) {
    return SaveAppIconPng(imagePath, config, size, errorText);
}

int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions) {
    AppConfig config = LoadRuntimeConfig(diagnosticsOptions, ConfigParseContext{TelemetryMetricCatalog()});
    Trace trace;
    DiagnosticsSession diagnostics(diagnosticsOptions, trace);
    if (!diagnostics.Initialize()) {
        return 1;
    }
    if (!ApplyDiagnosticsLayoutOverride(config, diagnosticsOptions, &diagnostics)) {
        return 1;
    }
    if (!ApplyDiagnosticsThemeOverride(config, diagnosticsOptions, &diagnostics)) {
        return 1;
    }

    diagnostics.WriteTraceMarker(
        "diagnostics:headless_start scale=" + std::to_string(ResolveSavedScreenshotScale(config)));
    WriteResolvedColorTrace(diagnostics, config);
    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialize_begin");

    std::string telemetryError;
    std::unique_ptr<TelemetryRuntime> telemetry =
        InitializeTelemetryRuntimeInstance(config, diagnosticsOptions, trace, nullptr, &telemetryError);
    if (telemetry == nullptr) {
        std::string traceText = "diagnostics:telemetry_initialize_failed";
        if (!telemetryError.empty()) {
            traceText += " detail=" + Trace::QuoteText(telemetryError);
        }
        diagnostics.WriteTraceMarker(traceText);
        if (diagnostics.ShouldShowDialogs()) {
            const std::wstring message = FormatTelemetryInitializeError(telemetryError);
            MessageBoxW(nullptr, message.c_str(), L"CaseDash", MB_ICONERROR);
        }
        return 1;
    }

    diagnostics.WriteTraceMarker("diagnostics:telemetry_initialized");
    Sleep(1000);
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_begin");
    TelemetryUpdate telemetryUpdate = telemetry->Latest();
    diagnostics.WriteTraceMarker("diagnostics:update_snapshot_done");
    if (diagnosticsOptions.reload) {
        std::string reloadError;
        if (!ReloadTelemetryCollectorFromDisk(GetRuntimeConfigPath(),
                config,
                telemetry,
                diagnosticsOptions,
                trace,
                &diagnostics,
                nullptr,
                &reloadError)) {
            if (diagnostics.ShouldShowDialogs() && !reloadError.empty()) {
                const std::wstring message = FormatTelemetryInitializeError(reloadError);
                MessageBoxW(nullptr, message.c_str(), L"CaseDash", MB_ICONERROR);
            }
            return 1;
        }
        telemetryUpdate = telemetry->Latest();
    }
    ApplyResolvedTelemetrySelections(config, telemetryUpdate.resolvedSelections);
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_begin");
    if (!diagnostics.WriteOutputs(telemetryUpdate.dump, config)) {
        diagnostics.WriteTraceMarker("diagnostics:write_outputs_failed");
        return 1;
    }
    diagnostics.WriteTraceMarker("diagnostics:write_outputs_done");
    diagnostics.WriteTraceMarker("diagnostics:headless_done");
    return 0;
}
