#include "diagnostics/diagnostics.h"

#include <windows.h>

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

#include "config/color_format.h"
#include "config/color_resolver.h"
#include "config/config_io.h"
#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "config/config_telemetry.h"
#include "config/config_writer.h"
#include "diagnostics/app_icon_export.h"
#include "diagnostics/constants.h"
#include "diagnostics/snapshot_dump.h"
#include "layout_edit/layout_edit_controller.h"
#include "layout_edit/layout_edit_tooltip_text.h"
#include "telemetry/metrics.h"
#include "telemetry/telemetry.h"
#include "util/command_line.h"
#include "util/paths.h"
#include "util/resource_strings.h"
#include "util/scale.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "widget/app_icon_geometry.h"

namespace {

constexpr char kAppendBinaryMode[] = "ab";
constexpr char kWriteBinaryMode[] = "wb";

std::string_view TrimAsciiView(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool TryParseInteger(std::string_view text, int& parsedValue) {
    text = TrimAsciiView(text);
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    std::string owned(text);
    const long value = std::strtol(owned.c_str(), &end, 10);
    if (end == owned.c_str() || end == nullptr || *end != '\0' || value < (std::numeric_limits<int>::min)() ||
        value > (std::numeric_limits<int>::max)()) {
        return false;
    }
    parsedValue = static_cast<int>(value);
    return true;
}

bool TryParseHoverPointValue(const std::string& text, DiagnosticsHoverPoint& point) {
    const size_t comma = text.find(',');
    if (comma == std::string::npos || text.find(',', comma + 1) != std::string::npos) {
        return false;
    }
    int x = 0;
    int y = 0;
    if (!TryParseInteger(std::string_view(text).substr(0, comma), x) ||
        !TryParseInteger(std::string_view(text).substr(comma + 1), y)) {
        return false;
    }
    point = DiagnosticsHoverPoint{x, y};
    return true;
}

void WriteResolvedColorTraceLine(
    DiagnosticsSession& diagnostics, std::string_view section, std::string_view name, const ColorConfig& color) {
    const std::string valueText = FormatRgbaColorText(color.ToRgba());
    if (!color.expression.empty()) {
        diagnostics.WriteTraceMarkerFmt(TracePrefix::Diagnostics,
            RES_STR("resolved_color section=\"%.*s\" name=\"%.*s\" value=\"%s\" expression=\"%s\""),
            static_cast<int>(section.size()),
            section.data(),
            static_cast<int>(name.size()),
            name.data(),
            valueText.c_str(),
            color.expression.c_str());
    } else {
        diagnostics.WriteTraceMarkerFmt(TracePrefix::Diagnostics,
            RES_STR("resolved_color section=\"%.*s\" name=\"%.*s\" value=\"%s\""),
            static_cast<int>(section.size()),
            section.data(),
            static_cast<int>(name.size()),
            name.data(),
            valueText.c_str());
    }
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

    const ColorsConfig& colors = config.layout.colors;
    for (const ColorsTraceField& field : kColorFields) {
        WriteResolvedColorTraceLine(diagnostics, "colors", field.name, colors.*field.color);
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

    void BeginLayoutEditTraceSession(ResourceStringId kind, const std::string& detail) override {
        (void)kind;
        (void)detail;
    }

    void RecordLayoutEditTracePhase(TracePhase phase, std::chrono::nanoseconds elapsed) override {
        (void)phase;
        (void)elapsed;
    }

    void EndLayoutEditTraceSession(ResourceStringId reason) override {
        (void)reason;
    }

private:
    const AppConfig& config_;
    DashboardRenderer& renderer_;
    DashboardOverlayState& overlayState_;
};

void WriteSaveErrorTrace(DiagnosticsSession& diagnostics,
    ResourceStringId traceEvent,
    const std::string& pathText,
    std::string_view detail,
    std::string_view traceSuffix) {
    const char* eventText = ResourceStringText(traceEvent);
    if (traceSuffix.empty() && detail.empty()) {
        diagnostics.WriteTraceMarkerFmt(
            TracePrefix::Diagnostics, RES_STR("%s path=\"%s\""), eventText, pathText.c_str());
    } else if (traceSuffix.empty()) {
        diagnostics.WriteTraceMarkerFmt(TracePrefix::Diagnostics,
            RES_STR("%s path=\"%s\" detail=\"%.*s\""),
            eventText,
            pathText.c_str(),
            static_cast<int>(detail.size()),
            detail.data());
    } else if (detail.empty()) {
        diagnostics.WriteTraceMarkerFmt(TracePrefix::Diagnostics,
            RES_STR("%s path=\"%s\" %.*s"),
            eventText,
            pathText.c_str(),
            static_cast<int>(traceSuffix.size()),
            traceSuffix.data());
    } else {
        diagnostics.WriteTraceMarkerFmt(TracePrefix::Diagnostics,
            RES_STR("%s path=\"%s\" %.*s detail=\"%.*s\""),
            eventText,
            pathText.c_str(),
            static_cast<int>(traceSuffix.size()),
            traceSuffix.data(),
            static_cast<int>(detail.size()),
            detail.data());
    }
}

}  // namespace

std::optional<double> TryParseScaleValue(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    std::string normalized;
    const char* parseText = text.c_str();
    if (text.find(',') != std::string::npos) {
        normalized = text;
        for (char& ch : normalized) {
            if (ch == ',') {
                ch = '.';
            }
        }
        parseText = normalized.c_str();
    }
    char* end = nullptr;
    const double value = std::strtod(parseText, &end);
    if (end == parseText || end == nullptr || *end != '\0' || !std::isfinite(value) || value <= 0.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> GetScaleSwitchValue(const CommandLineArguments& commandLine) {
    if (const auto value = GetColonSwitchValue(commandLine, "/scale"); value.has_value()) {
        return TryParseScaleValue(*value);
    }
    return std::nullopt;
}

std::optional<int> TryParseAppIconSizeValue(const std::string& text) {
    int value = 0;
    if (!TryParseInteger(text, value) || !IsValidAppIconSize(value)) {
        return std::nullopt;
    }
    return value;
}

void AssignTrimmedColonSwitchValue(const CommandLineArguments& commandLine, const char* name, std::string& target) {
    if (const auto value = GetColonSwitchValue(commandLine, name); value.has_value()) {
        const std::string_view trimmed = TrimAsciiView(*value);
        if (!trimmed.empty()) {
            target.assign(trimmed);
        }
    }
}

bool TryParseTracePrefixFilter(std::string_view text, std::uint64_t& mask, std::string& invalidName) {
    mask = 0;
    invalidName.clear();
    bool hasName = false;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t delimiter = text.find(',', start);
        std::string_view name =
            delimiter == std::string_view::npos ? text.substr(start) : text.substr(start, delimiter - start);
        name = TrimAsciiView(name);
        if (!name.empty()) {
            hasName = true;
            const std::optional<TracePrefix> prefix = Trace::ParsePrefixName(name);
            if (!prefix.has_value()) {
                invalidName.assign(name);
                return false;
            }
            mask |= Trace::PrefixMask(*prefix);
        }
        if (delimiter == std::string_view::npos) {
            break;
        }
        start = delimiter + 1;
    }

    if (!hasName) {
        invalidName.clear();
        return false;
    }

    return true;
}

void WriteValidationFailureTrace(
    const DiagnosticsOptions& options, const std::string& reason, const std::string& message) {
    if (!options.trace) {
        return;
    }

    const FilePath tracePath =
        ResolveDiagnosticsOutputPath(GetWorkingDirectory(), options.tracePath, kDefaultTraceFileName);
    std::FILE* traceFile = nullptr;
    if (fopen_s(&traceFile, tracePath.string().c_str(), kAppendBinaryMode) != 0 || traceFile == nullptr) {
        return;
    }

    Trace trace(traceFile);
    trace.WriteFmt(TracePrefix::Diagnostics,
        RES_STR("validation_failed reason=\"%s\" message=\"%s\""),
        reason.c_str(),
        message.c_str());
    fclose(traceFile);
}

DiagnosticsValidationResult DiagnosticsValidationFailure(
    const DiagnosticsOptions& options, std::string reason, std::string message) {
    WriteValidationFailureTrace(options, reason, message);
    DiagnosticsValidationResult result;
    result.ok = false;
    result.reason = std::move(reason);
    result.message = std::move(message);
    return result;
}

std::string FormatTraceDetailText(std::string_view detail) {
    std::string text;
    text.reserve(detail.size());
    for (const char ch : detail) {
        switch (ch) {
            case '\\':
                text += "\\\\";
                break;
            case '"':
                text += "\\\"";
                break;
            case '\r':
                text += "\\r";
                break;
            case '\n':
                text += "\\n";
                break;
            default:
                text += ch;
                break;
        }
    }
    return text;
}

void WriteStandaloneDiagnosticsFailureTrace(
    const DiagnosticsOptions& options, ResourceStringId event, std::string_view detail) {
    if (!options.trace) {
        return;
    }

    const FilePath tracePath =
        ResolveDiagnosticsOutputPath(GetWorkingDirectory(), options.tracePath, kDefaultTraceFileName);
    std::FILE* traceFile = nullptr;
    if (fopen_s(&traceFile, tracePath.string().c_str(), kAppendBinaryMode) != 0 || traceFile == nullptr) {
        return;
    }

    Trace trace(traceFile);
    if (detail.empty()) {
        trace.Write(TracePrefix::Diagnostics, event);
    } else {
        const std::string detailText = FormatTraceDetailText(detail);
        trace.WriteFmt(TracePrefix::Diagnostics,
            RES_STR("%s detail=\"%.*s\""),
            ResourceStringText(event),
            static_cast<int>(detailText.size()),
            detailText.data());
    }
    fclose(traceFile);
}

std::string FormatDiagnosticsFailureMessage(std::string_view action, std::string_view detail) {
    std::string message;
    AssignFormat(message, "Failed to %.*s.", static_cast<int>(action.size()), action.data());
    if (!detail.empty()) {
        AppendFormat(message, "\n\n%.*s", static_cast<int>(detail.size()), detail.data());
    }
    return message;
}

struct DiagnosticsPlainSwitch {
    const char* name;
    bool DiagnosticsOptions::* enabled;
};

struct DiagnosticsPathSwitch {
    const char* name;
    bool DiagnosticsOptions::* enabled;
    FilePath DiagnosticsOptions::* path;
};

void ApplyDiagnosticsPlainSwitches(DiagnosticsOptions& options, const CommandLineArguments& commandLine) {
    static constexpr DiagnosticsPlainSwitch kSwitches[] = {
        {"/exit", &DiagnosticsOptions::exit},
        {"/blank", &DiagnosticsOptions::blank},
        {"/edit-layout", &DiagnosticsOptions::editLayout},
        {"/reload", &DiagnosticsOptions::reload},
        {"/default-config", &DiagnosticsOptions::defaultConfig},
    };

    for (const DiagnosticsPlainSwitch& entry : kSwitches) {
        options.*entry.enabled = HasSwitch(commandLine, entry.name);
    }
}

void ApplyDiagnosticsPathSwitches(DiagnosticsOptions& options, const CommandLineArguments& commandLine) {
    static constexpr DiagnosticsPathSwitch kSwitches[] = {
        {"/trace", &DiagnosticsOptions::trace, &DiagnosticsOptions::tracePath},
        {"/dump", &DiagnosticsOptions::dump, &DiagnosticsOptions::dumpPath},
        {"/screenshot", &DiagnosticsOptions::screenshot, &DiagnosticsOptions::screenshotPath},
        {"/layout-guide-sheet", &DiagnosticsOptions::layoutGuideSheet, &DiagnosticsOptions::layoutGuideSheetPath},
        {"/app-icon", &DiagnosticsOptions::appIcon, &DiagnosticsOptions::appIconPath},
        {"/save-config", &DiagnosticsOptions::saveConfig, &DiagnosticsOptions::saveConfigPath},
        {"/save-full-config", &DiagnosticsOptions::saveFullConfig, &DiagnosticsOptions::saveFullConfigPath},
        {"/fake", &DiagnosticsOptions::fake, &DiagnosticsOptions::fakePath},
    };

    for (const DiagnosticsPathSwitch& entry : kSwitches) {
        if (const auto value = GetColonSwitchValue(commandLine, entry.name); value.has_value()) {
            options.*entry.enabled = true;
            options.*entry.path = FilePath(*value);
        } else {
            options.*entry.enabled = HasSwitch(commandLine, entry.name);
        }
    }

    if (const auto value = GetColonSwitchValue(commandLine, "/trace-prefixes"); value.has_value()) {
        options.trace = true;
        std::uint64_t prefixFilter = 0;
        std::string invalidName;
        if (TryParseTracePrefixFilter(*value, prefixFilter, invalidName)) {
            options.hasTracePrefixFilter = true;
            options.tracePrefixFilter = prefixFilter;
        } else {
            options.hasInvalidTracePrefixFilter = true;
            options.invalidTracePrefixFilterName = invalidName;
        }
    } else if (HasSwitch(commandLine, "/trace-prefixes")) {
        options.trace = true;
        options.hasInvalidTracePrefixFilter = true;
    }
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

DiagnosticsOptions GetDiagnosticsOptions(const CommandLineArguments& commandLine) {
    DiagnosticsOptions options;
    ApplyDiagnosticsPlainSwitches(options, commandLine);
    ApplyDiagnosticsPathSwitches(options, commandLine);
    if (const auto editLayoutValue = GetColonSwitchValue(commandLine, "/edit-layout"); editLayoutValue.has_value()) {
        const std::string mode = ToLower(Trim(*editLayoutValue));
        options.editLayout = true;
        if (mode == "horizontal-sizes" || mode == "horizonatal-sizes") {
            options.layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::HorizontalSizes;
        } else if (mode == "vertical-sizes") {
            options.layoutSimilarityMode = DiagnosticsLayoutSimilarityMode::VerticalSizes;
        } else if (!mode.empty()) {
            options.editLayoutWidgetName = mode;
        }
    }
    AssignTrimmedColonSwitchValue(commandLine, "/layout", options.layoutName);
    AssignTrimmedColonSwitchValue(commandLine, "/theme", options.themeName);
    if (const auto scale = GetScaleSwitchValue(commandLine); scale.has_value()) {
        options.hasScaleOverride = true;
        options.scale = *scale;
    }
    if (const auto appIconSizeValue = GetColonSwitchValue(commandLine, "/app-icon-size");
        appIconSizeValue.has_value()) {
        options.hasAppIconSize = true;
        if (const auto appIconSize = TryParseAppIconSizeValue(*appIconSizeValue); appIconSize.has_value()) {
            options.appIconSize = *appIconSize;
        } else {
            options.appIconSize = 0;
        }
    }
    if (const auto hoverValue = GetColonSwitchValue(commandLine, "/hover"); hoverValue.has_value()) {
        DiagnosticsHoverPoint hoverPoint;
        if (TryParseHoverPointValue(*hoverValue, hoverPoint)) {
            options.hoverPoint = hoverPoint;
            options.editLayout = true;
        }
    }
    return options;
}

DiagnosticsValidationResult ValidateDiagnosticsOptions(
    const DiagnosticsOptions& options, DiagnosticsOutputHandlers handlers) {
    if (options.hasInvalidTracePrefixFilter) {
        const std::string prefixNames = Trace::PrefixNamesText();
        std::string message = FormatText(
            "/trace-prefixes must contain a comma-separated list of trace prefixes: %s.", prefixNames.c_str());
        if (!options.invalidTracePrefixFilterName.empty()) {
            AppendFormat(message, " Unknown prefix: %s.", options.invalidTracePrefixFilterName.c_str());
        }
        return DiagnosticsValidationFailure(options, "trace_prefixes", message);
    }
    if (options.layoutGuideSheet && handlers.writeExtraOutputs == nullptr) {
        return DiagnosticsValidationFailure(
            options, "layout_guide_sheet_unavailable", "Use CaseDashHeadless.exe for /layout-guide-sheet.");
    }
    if (options.blank && options.fake) {
        return DiagnosticsValidationFailure(
            options, "blank_fake_conflict", "/blank cannot be used together with /fake.");
    }
    if (options.blank && options.layoutGuideSheet) {
        return DiagnosticsValidationFailure(
            options, "blank_layout_guide_sheet_conflict", "/blank cannot be used together with /layout-guide-sheet.");
    }
    if (options.hasAppIconSize && !IsValidAppIconSize(options.appIconSize)) {
        return DiagnosticsValidationFailure(
            options, "app_icon_size", "/app-icon-size must be between 16 and 1024 pixels.");
    }
    return {};
}

void ReportDiagnosticsError(const DiagnosticsOptions& options, std::string_view message) {
    if (options.reportError != nullptr && !message.empty()) {
        options.reportError(options, message);
    }
}

bool ApplyDiagnosticsLayoutOverride(
    AppConfig& config, const DiagnosticsOptions& options, DiagnosticsSession* diagnostics, std::string* errorText) {
    if (errorText != nullptr) {
        errorText->clear();
    }
    if (options.layoutName.empty()) {
        return true;
    }
    if (SelectLayout(config, options.layoutName)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarkerFmt(
                TracePrefix::Diagnostics, RES_STR("layout_override name=\"%s\""), options.layoutName.c_str());
        }
        return true;
    }

    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarkerFmt(
            TracePrefix::Diagnostics, RES_STR("layout_override_failed name=\"%s\""), options.layoutName.c_str());
    }
    if (errorText != nullptr) {
        *errorText = FormatText("Unknown layout name:\n%s", options.layoutName.c_str());
    }
    return false;
}

bool ApplyDiagnosticsThemeOverride(AppConfig& config,
    const DiagnosticsOptions& options,
    DiagnosticsSession* diagnostics,
    std::string* errorText,
    ResolveDiagnosticsExtraConfigFn resolveExtraConfig) {
    if (errorText != nullptr) {
        errorText->clear();
    }
    if (options.themeName.empty()) {
        return true;
    }
    for (const ThemeConfig& theme : config.layout.themes) {
        if (theme.name == options.themeName) {
            config.display.theme = options.themeName;
            ResolveConfiguredColors(config);
            if (resolveExtraConfig != nullptr) {
                resolveExtraConfig(config);
            }
            if (diagnostics != nullptr) {
                diagnostics->WriteTraceMarkerFmt(
                    TracePrefix::Diagnostics, RES_STR("theme_override name=\"%s\""), options.themeName.c_str());
            }
            return true;
        }
    }

    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarkerFmt(
            TracePrefix::Diagnostics, RES_STR("theme_override_failed name=\"%s\""), options.themeName.c_str());
    }
    if (errorText != nullptr) {
        *errorText = FormatText("Unknown theme name:\n%s", options.themeName.c_str());
    }
    return false;
}

double ResolveSavedScreenshotScale(const AppConfig& config) {
    return HasExplicitDisplayScale(config.display.scale) ? config.display.scale : 1.0;
}

DiagnosticsSession::DiagnosticsSession(
    const DiagnosticsOptions& options, Trace& trace, DiagnosticsOutputHandlers handlers)
    : options_(options), trace_(trace), handlers_(handlers) {}

DiagnosticsSession::~DiagnosticsSession() {
    if (traceFile_ != nullptr) {
        trace_.SetOutput(nullptr);
        trace_.SetEnabledPrefixes(Trace::AllPrefixesMask());
        fclose(traceFile_);
    }
}

bool DiagnosticsSession::Initialize() {
    const FilePath workingDirectory = GetWorkingDirectory();

    struct OutputPath {
        bool DiagnosticsOptions::* enabled;
        FilePath DiagnosticsOptions::* configuredPath;
        FilePath DiagnosticsSession::* resolvedPath;
        const char* defaultFileName;
    };

    static constexpr OutputPath kOutputPaths[] = {
        {&DiagnosticsOptions::trace,
            &DiagnosticsOptions::tracePath,
            &DiagnosticsSession::tracePath_,
            kDefaultTraceFileName},
        {&DiagnosticsOptions::dump,
            &DiagnosticsOptions::dumpPath,
            &DiagnosticsSession::dumpPath_,
            kDefaultDumpFileName},
        {&DiagnosticsOptions::screenshot,
            &DiagnosticsOptions::screenshotPath,
            &DiagnosticsSession::screenshotPath_,
            kDefaultScreenshotFileName},
        {&DiagnosticsOptions::appIcon,
            &DiagnosticsOptions::appIconPath,
            &DiagnosticsSession::appIconPath_,
            kDefaultAppIconFileName},
        {&DiagnosticsOptions::saveConfig,
            &DiagnosticsOptions::saveConfigPath,
            &DiagnosticsSession::saveConfigPath_,
            kDefaultSavedConfigFileName},
        {&DiagnosticsOptions::saveFullConfig,
            &DiagnosticsOptions::saveFullConfigPath,
            &DiagnosticsSession::saveFullConfigPath_,
            kDefaultSavedFullConfigFileName},
    };

    for (const OutputPath& outputPath : kOutputPaths) {
        if (options_.*outputPath.enabled) {
            this->*outputPath.resolvedPath = ResolveDiagnosticsOutputPath(
                workingDirectory, options_.*outputPath.configuredPath, outputPath.defaultFileName);
        }
    }
    if (options_.trace) {
        if (fopen_s(&traceFile_, tracePath_.string().c_str(), kAppendBinaryMode) != 0 || traceFile_ == nullptr) {
            ShowFileOpenError("trace file", tracePath_);
            return false;
        }
        trace_.SetEnabledPrefixes(
            options_.hasTracePrefixFilter ? options_.tracePrefixFilter : Trace::AllPrefixesMask());
        trace_.SetOutput(traceFile_);
    }
    return true;
}

bool DiagnosticsSession::ShouldShowDialogs() const {
    return !options_.trace;
}

const std::string& DiagnosticsSession::LastError() const {
    return lastError_;
}

void DiagnosticsSession::WriteTraceMarker(TracePrefix prefix, const char* text) {
    trace_.Write(prefix, text);
}

void DiagnosticsSession::WriteTraceMarker(TracePrefix prefix, ResourceStringId text) {
    trace_.Write(prefix, text);
}

void DiagnosticsSession::WriteTraceMarker(TracePrefix prefix, const std::string& text) {
    trace_.Write(prefix, text);
}

void DiagnosticsSession::WriteTraceMarkerWithDetail(
    TracePrefix prefix, ResourceStringId text, std::string_view detail) {
    if (detail.empty()) {
        WriteTraceMarker(prefix, text);
        return;
    }
    const std::string detailText = FormatTraceDetailText(detail);
    WriteTraceMarkerFmt(prefix,
        RES_STR("%s detail=\"%.*s\""),
        ResourceStringText(text),
        static_cast<int>(detailText.size()),
        detailText.data());
}

void DiagnosticsSession::WriteTraceMarkerFmt(TracePrefix prefix, const char* format, ...) {
    va_list args;
    va_start(args, format);
    WriteTraceMarkerVFmt(prefix, format, args);
    va_end(args);
}

void DiagnosticsSession::WriteTraceMarkerFmt(TracePrefix prefix, ResourceStringId format, ...) {
    va_list args;
    va_start(args, format);
    WriteTraceMarkerVFmt(prefix, format, args);
    va_end(args);
}

void DiagnosticsSession::WriteTraceMarkerVFmt(TracePrefix prefix, const char* format, va_list args) {
    trace_.WriteVFmt(prefix, format, args);
}

void DiagnosticsSession::WriteTraceMarkerVFmt(TracePrefix prefix, ResourceStringId format, va_list args) {
    trace_.WriteVFmt(prefix, format, args);
}

void DiagnosticsSession::ReportError(TracePrefix prefix, const std::string& traceText, std::string_view message) {
    WriteTraceMarker(prefix, traceText);
    lastError_.assign(message);
}

bool DiagnosticsSession::ReportSaveError(ResourceStringId traceEvent,
    const char* messageAction,
    const FilePath& path,
    std::string_view detail,
    std::string_view traceSuffix) {
    const std::string pathText = path.string();
    const std::string message = FormatText("Failed to %s:\n%s", messageAction, pathText.c_str());
    WriteSaveErrorTrace(*this, traceEvent, pathText, detail, traceSuffix);
    lastError_ = message;
    if (!detail.empty()) {
        AppendFormat(lastError_, "\n\n%.*s", static_cast<int>(detail.size()), detail.data());
    }
    return false;
}

bool DiagnosticsSession::WriteOutputs(const TelemetryDump& dump, const AppConfig& config) {
    if (options_.dump) {
        std::FILE* dumpFile = nullptr;
        if (fopen_s(&dumpFile, dumpPath_.string().c_str(), kWriteBinaryMode) != 0 || dumpFile == nullptr) {
            ShowFileOpenError("dump file", dumpPath_);
            return false;
        }
        const bool dumpWritten = WriteTelemetryDump(dumpFile, dump);
        fclose(dumpFile);
        if (!dumpWritten) {
            return ReportSaveError(RES_STR("dump_write_failed"), "write dump file", dumpPath_);
        }
    }

    const double savedScreenshotScale = ResolveSavedScreenshotScale(config);
    if (options_.screenshot) {
        std::string screenshotError;
        if (!SaveDumpScreenshot(screenshotPath_,
                dump.snapshot,
                config,
                savedScreenshotScale,
                GetDiagnosticsRenderMode(options_),
                options_.editLayout,
                GetSimilarityIndicatorMode(options_),
                options_.editLayoutWidgetName,
                trace_,
                options_.hoverPoint.has_value(),
                options_.hoverPoint.has_value() ? RenderPoint{options_.hoverPoint->x, options_.hoverPoint->y}
                                                : RenderPoint{},
                &screenshotError)) {
            return ReportSaveError(
                RES_STR("screenshot_save_failed"), "save screenshot", screenshotPath_, screenshotError);
        }
    }

    if (handlers_.writeExtraOutputs != nullptr) {
        std::string extraOutputError;
        if (!handlers_.writeExtraOutputs(
                handlers_.context, options_, dump, config, savedScreenshotScale, trace_, &extraOutputError)) {
            lastError_ = extraOutputError;
            return false;
        }
    }

    if (options_.appIcon) {
        std::string appIconError;
        if (!SaveRenderedAppIcon(appIconPath_, config, options_.appIconSize, &appIconError)) {
            return ReportSaveError(RES_STR("app_icon_save_failed"),
                "save app icon",
                appIconPath_,
                appIconError,
                FormatText(RES_STR("size=%d"), options_.appIconSize));
        }
        const std::string pathText = appIconPath_.string();
        WriteTraceMarkerFmt(TracePrefix::Diagnostics,
            RES_STR("app_icon_saved path=\"%s\" size=%d"),
            pathText.c_str(),
            options_.appIconSize);
    }

    if (options_.saveConfig && !SaveConfig(saveConfigPath_, config, ConfigParseContext{TelemetryMetricCatalog()})) {
        return ReportSaveError(RES_STR("config_save_failed"), "save config file", saveConfigPath_);
    }

    if (options_.saveFullConfig && !SaveFullConfig(saveFullConfigPath_, config)) {
        return ReportSaveError(RES_STR("full_config_save_failed"), "save full config file", saveFullConfigPath_);
    }

    return true;
}

void DiagnosticsSession::ShowFileOpenError(const char* label, const FilePath& path) {
    const std::string pathText = path.string();
    const std::string message = FormatText("Failed to open %s:\n%s", label, pathText.c_str());
    ReportError(TracePrefix::Diagnostics,
        FormatText(RES_STR("file_open_failed label=\"%s\" path=\"%s\""), label, pathText.c_str()),
        message);
}

FilePath ResolveDiagnosticsOutputPath(
    const FilePath& workingDirectory, const FilePath& configuredPath, std::string_view defaultFileName) {
    if (configuredPath.empty()) {
        return workingDirectory / FilePath(defaultFileName);
    }
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return workingDirectory / configuredPath;
}

TelemetryCollectorOptions BuildTelemetryCollectorOptions(const DiagnosticsOptions& diagnosticsOptions) {
    TelemetryCollectorOptions options;
    options.fake = diagnosticsOptions.fake;
    options.liveFake = diagnosticsOptions.fake && !diagnosticsOptions.exit;
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

std::string FormatTelemetryInitializeError(std::string_view errorText) {
    std::string message = ResourceStringText(RES_STR("Failed to initialize telemetry collector."));
    if (!errorText.empty()) {
        AppendFormat(message, "\n\n%.*s", static_cast<int>(errorText.size()), errorText.data());
    }
    return message;
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
    std::string* errorText,
    std::string_view extraTemplate,
    ResolveDiagnosticsExtraConfigFn resolveExtraConfig) {
    if (errorText != nullptr) {
        errorText->clear();
    }
    const AppConfig reloadedConfig = LoadConfigWithExtraTemplate(
        configPath, !diagnosticsOptions.defaultConfig, ConfigParseContext{TelemetryMetricCatalog()}, extraTemplate);
    AppConfig effectiveReloadedConfig = reloadedConfig;
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("reload_config_begin"));
    }
    if (!ApplyDiagnosticsLayoutOverride(effectiveReloadedConfig, diagnosticsOptions, diagnostics, errorText)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("reload_config_failed"));
        }
        return false;
    }
    if (!ApplyDiagnosticsThemeOverride(
            effectiveReloadedConfig, diagnosticsOptions, diagnostics, errorText, resolveExtraConfig)) {
        if (diagnostics != nullptr) {
            diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("reload_config_failed"));
        }
        return false;
    }
    ApplyDiagnosticsScaleOverride(effectiveReloadedConfig, diagnosticsOptions);
    if (resolveExtraConfig != nullptr) {
        resolveExtraConfig(effectiveReloadedConfig);
    }

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
            diagnostics->WriteTraceMarkerWithDetail(
                TracePrefix::Diagnostics, RES_STR("reload_config_failed"), reloadError);
        }
        return false;
    }

    telemetry = std::move(reloadedTelemetry);
    const TelemetryUpdate reloadedUpdate = telemetry->Latest();
    activeConfig = std::move(effectiveReloadedConfig);
    ApplyResolvedTelemetrySelections(activeConfig, reloadedUpdate.resolvedSelections);
    if (diagnostics != nullptr) {
        diagnostics->WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("reload_config_done"));
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
    bool hasHoverPoint,
    RenderPoint hoverPoint,
    std::string* errorText) {
    DashboardRenderer renderer(trace);
    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = showLayoutEditGuides || hasHoverPoint;
    overlayState.forceLayoutEditAffordances = showLayoutEditGuides && !hasHoverPoint;
    overlayState.similarityIndicatorMode = similarityIndicatorMode;
    renderer.SetRenderScale(scale);
    renderer.SetConfig(config);
    renderer.SetRenderMode(renderMode);
    if (!renderer.Initialize()) {
        const std::string error = renderer.LastError();
        if (errorText != nullptr) {
            *errorText = error;
        }
        WriteRendererErrorTrace(trace, RES_STR("screenshot_initialize"), error);
        return false;
    }
    if (!editLayoutWidgetName.empty()) {
        const auto widget = renderer.FindFirstLayoutEditPreviewWidget(editLayoutWidgetName);
        if (!widget.has_value()) {
            const std::string error =
                FormatText(RES_STR("edit_layout_widget_not_found name=\"%s\""), editLayoutWidgetName.c_str());
            if (errorText != nullptr) {
                *errorText = error;
            }
            WriteRendererErrorTrace(trace, RES_STR("screenshot_edit_layout_widget"), error);
            return false;
        }
        overlayState.SetPreviewWidget(*widget);
        trace.WriteFmt(
            TracePrefix::Diagnostics, RES_STR("edit_layout_widget name=\"%s\""), editLayoutWidgetName.c_str());
    }
    if (hasHoverPoint) {
        if (!renderer.PrimeLayoutEditDynamicRegions(snapshot, overlayState)) {
            const std::string error = renderer.LastError();
            if (errorText != nullptr) {
                *errorText = error;
            }
            WriteRendererErrorTrace(trace, RES_STR("screenshot_hover_regions"), error);
            return false;
        }

        DiagnosticsLayoutEditHost host(config, renderer, overlayState);
        LayoutEditController controller(host);
        controller.HandleMouseMove(hoverPoint);
        LayoutEditController::TooltipTarget target;
        if (controller.CurrentTooltipTarget(target)) {
            std::string tooltipError;
            std::string tooltipText;
            const bool hasTooltipText =
                BuildLayoutEditTooltipTextForPayload(config, target.payload, tooltipText, &tooltipError);
            if (hasTooltipText) {
                trace.WriteFmt(TracePrefix::Diagnostics,
                    RES_STR("hover point=\"%d,%d\" target=\"%s\" tooltip=\"%s\""),
                    hoverPoint.x,
                    hoverPoint.y,
                    LayoutEditTooltipPayloadTraceKind(target.payload),
                    tooltipText.c_str());
            } else {
                const char* tooltipErrorText = tooltipError.empty() ? "unsupported_target" : tooltipError.c_str();
                trace.WriteFmt(TracePrefix::Diagnostics,
                    RES_STR("hover point=\"%d,%d\" target=\"%s\" tooltip_error=\"%s\""),
                    hoverPoint.x,
                    hoverPoint.y,
                    LayoutEditTooltipPayloadTraceKind(target.payload),
                    tooltipErrorText);
            }
        } else {
            trace.WriteFmt(
                TracePrefix::Diagnostics, RES_STR("hover point=\"%d,%d\" target=\"none\""), hoverPoint.x, hoverPoint.y);
        }
    }
    const bool saved = renderer.SaveSnapshotPng(imagePath, snapshot, overlayState);
    if (!saved) {
        const std::string error = renderer.LastError();
        if (errorText != nullptr) {
            *errorText = error;
        }
        WriteRendererErrorTrace(trace, RES_STR("screenshot_save"), error);
    }
    return saved;
}

bool SaveRenderedAppIcon(const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText) {
    return SaveAppIconPng(imagePath, config, size, errorText);
}

int RunDiagnosticsHeadlessMode(const DiagnosticsOptions& diagnosticsOptions, DiagnosticsOutputHandlers handlers) {
    std::string extraConfigTemplate;
    if (handlers.loadExtraConfig != nullptr) {
        std::string extraConfigError;
        if (!handlers.loadExtraConfig(&extraConfigTemplate, &extraConfigError)) {
            const std::string message =
                FormatDiagnosticsFailureMessage("load diagnostics config extension", extraConfigError);
            WriteStandaloneDiagnosticsFailureTrace(
                diagnosticsOptions, RES_STR("headless_extra_config_load_failed"), message);
            ReportDiagnosticsError(diagnosticsOptions, message);
            return 1;
        }
    }

    AppConfig config = LoadRuntimeConfigWithExtraTemplate(
        diagnosticsOptions, ConfigParseContext{TelemetryMetricCatalog()}, extraConfigTemplate);
    if (handlers.resolveExtraConfig != nullptr) {
        handlers.resolveExtraConfig(config);
    }
    Trace trace;
    DiagnosticsSession diagnostics(diagnosticsOptions, trace, handlers);
    if (!diagnostics.Initialize()) {
        ReportDiagnosticsError(diagnosticsOptions, diagnostics.LastError());
        return 1;
    }
    std::string overrideError;
    if (!ApplyDiagnosticsLayoutOverride(config, diagnosticsOptions, &diagnostics, &overrideError)) {
        diagnostics.WriteTraceMarkerWithDetail(
            TracePrefix::Diagnostics, RES_STR("headless_layout_override_failed"), overrideError);
        ReportDiagnosticsError(diagnosticsOptions, overrideError);
        return 1;
    }
    if (!ApplyDiagnosticsThemeOverride(
            config, diagnosticsOptions, &diagnostics, &overrideError, handlers.resolveExtraConfig)) {
        diagnostics.WriteTraceMarkerWithDetail(
            TracePrefix::Diagnostics, RES_STR("headless_theme_override_failed"), overrideError);
        ReportDiagnosticsError(diagnosticsOptions, overrideError);
        return 1;
    }

    diagnostics.WriteTraceMarkerFmt(
        TracePrefix::Diagnostics, RES_STR("headless_start scale=%.6f"), ResolveSavedScreenshotScale(config));
    WriteResolvedColorTrace(diagnostics, config);
    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("telemetry_initialize_begin"));

    std::string telemetryError;
    std::unique_ptr<TelemetryRuntime> telemetry =
        InitializeTelemetryRuntimeInstance(config, diagnosticsOptions, trace, nullptr, &telemetryError);
    if (telemetry == nullptr) {
        diagnostics.WriteTraceMarkerWithDetail(
            TracePrefix::Diagnostics, RES_STR("telemetry_initialize_failed"), telemetryError);
        ReportDiagnosticsError(diagnosticsOptions, FormatTelemetryInitializeError(telemetryError));
        return 1;
    }

    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("telemetry_initialized"));
    Sleep(1000);
    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("update_snapshot_begin"));
    TelemetryUpdate telemetryUpdate = telemetry->Latest();
    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("update_snapshot_done"));
    if (diagnosticsOptions.reload) {
        std::string reloadError;
        if (!ReloadTelemetryCollectorFromDisk(GetRuntimeConfigPath(),
                config,
                telemetry,
                diagnosticsOptions,
                trace,
                &diagnostics,
                nullptr,
                &reloadError,
                extraConfigTemplate,
                handlers.resolveExtraConfig)) {
            const std::string message = FormatDiagnosticsFailureMessage("reload config", reloadError);
            diagnostics.WriteTraceMarkerWithDetail(
                TracePrefix::Diagnostics, RES_STR("headless_reload_config_failed"), message);
            ReportDiagnosticsError(diagnosticsOptions, message);
            return 1;
        }
        telemetryUpdate = telemetry->Latest();
    }
    ApplyResolvedTelemetrySelections(config, telemetryUpdate.resolvedSelections);
    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("write_outputs_begin"));
    if (!diagnostics.WriteOutputs(telemetryUpdate.dump, config)) {
        diagnostics.WriteTraceMarkerWithDetail(
            TracePrefix::Diagnostics, RES_STR("write_outputs_failed"), diagnostics.LastError());
        ReportDiagnosticsError(diagnosticsOptions, diagnostics.LastError());
        return 1;
    }
    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("write_outputs_done"));
    diagnostics.WriteTraceMarker(TracePrefix::Diagnostics, RES_STR("headless_done"));
    return 0;
}
