#include "layout_guide_sheet/layout_guide_sheet.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "config/color_resolver.h"
#include "config/config.h"
#include "config/config_parser.h"
#include "config/config_runtime_fields.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "dashboard_renderer/layout_guide_sheet_support.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_planner.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_renderer.h"
#include "layout_model/dashboard_overlay_state.h"
#include "util/localization_catalog.h"
#include "util/numeric_format.h"
#include "util/trace.h"

namespace {

using Clock = std::chrono::steady_clock;

double Milliseconds(std::chrono::nanoseconds elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void RecordStats(std::chrono::nanoseconds LayoutGuideSheetPipelineStats::* field,
    Clock::time_point start,
    LayoutGuideSheetPipelineStats* stats) {
    if (stats != nullptr) {
        (*stats).*field += Clock::now() - start;
    }
}

bool BuildLayoutGuideSheetPipelineInputs(DashboardRenderer& renderer,
    const SystemSnapshot& snapshot,
    const LayoutGuideSheetConfig& guideSheet,
    std::vector<LayoutGuideSheetCalloutRequest>& callouts,
    std::vector<std::string>& selectedCardIds,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;
    overlayState.forceLayoutEditAffordances = true;
    overlayState.hoverOnExposedDashboard = true;

    const auto activeRegionsStart = Clock::now();
    if (!renderer.PrimeLayoutEditDynamicRegions(snapshot, overlayState)) {
        if (errorText != nullptr) {
            *errorText = renderer.LastError();
        }
        return false;
    }
    const std::vector<LayoutGuideSheetCardSummary> cards = CollectLayoutGuideSheetCardSummaries(renderer);
    const LayoutEditActiveRegions activeRegions = renderer.CollectLayoutEditActiveRegions(overlayState);
    LayoutGuideSheetRenderer sheetRenderer(renderer, guideSheet);
    const LayoutEditActiveRegions overviewActiveRegions = sheetRenderer.CollectOverviewActiveRegions(snapshot);
    RecordStats(&LayoutGuideSheetPipelineStats::activeRegions, activeRegionsStart, stats);

    InitializeLocalizationCatalog();
    const auto planStart = Clock::now();
    selectedCardIds = SelectLayoutGuideSheetCards(cards);
    BuildLayoutGuideSheetOverviewCallouts(renderer.Config(), overviewActiveRegions, callouts);
    std::vector<LayoutGuideSheetCalloutRequest> cardCallouts;
    BuildLayoutGuideSheetCallouts(renderer.Config(), activeRegions, cards, selectedCardIds, cardCallouts);
    AppendLayoutGuideSheetCardCallouts(callouts, cardCallouts);
    RecordStats(&LayoutGuideSheetPipelineStats::plan, planStart, stats);

    if (stats != nullptr) {
        stats->selectedCards = selectedCardIds.size();
        stats->callouts = callouts.size();
    }
    return true;
}

void RecordRenderStats(const LayoutGuideSheetRenderStats& renderStats, LayoutGuideSheetPipelineStats* stats) {
    if (stats == nullptr) {
        return;
    }
    stats->measure += renderStats.measure;
    stats->placement += renderStats.placement;
    stats->draw += renderStats.draw;
}

void WritePipelineStatsTrace(Trace& trace, const LayoutGuideSheetPipelineStats& stats) {
    const std::string activeRegionsText = FormatDoubleFixed(Milliseconds(stats.activeRegions), 3);
    const std::string planText = FormatDoubleFixed(Milliseconds(stats.plan), 3);
    const std::string measureText = FormatDoubleFixed(Milliseconds(stats.measure), 3);
    const std::string placementText = FormatDoubleFixed(Milliseconds(stats.placement), 3);
    const std::string drawText = FormatDoubleFixed(Milliseconds(stats.draw), 3);
    trace.WriteFmt(TracePrefix::Diagnostics,
        RES_STR("layout_guide_sheet stats selected_cards=%zu callouts=%zu active_regions_ms=%s sheet_plan_ms=%s "
                "sheet_measure_ms=%s sheet_place_ms=%s sheet_draw_ms=%s"),
        stats.selectedCards,
        stats.callouts,
        activeRegionsText.c_str(),
        planText.c_str(),
        measureText.c_str(),
        placementText.c_str(),
        drawText.c_str());
}

struct LayoutGuideSheetParseContext {
    LayoutGuideSheetConfig* guideSheet = nullptr;
};

void ApplyLayoutGuideSheetConfigEntry(
    void* context, std::string_view section, std::string_view key, std::string_view value) {
    if (section != "layout_guide_sheet") {
        return;
    }

    auto& parseContext = *static_cast<LayoutGuideSheetParseContext*>(context);
    for (const RuntimeConfigFieldDescriptor& field : LayoutGuideSheetRuntimeConfigFields()) {
        if (std::string_view(field.key, field.keyLength) == key) {
            DecodeRuntimeConfigField(field, parseContext.guideSheet, std::string(value));
            return;
        }
    }
}

const ThemeConfig* FindActiveTheme(const AppConfig& config) {
    for (const ThemeConfig& theme : config.layout.themes) {
        if (theme.name == config.display.theme) {
            return &theme;
        }
    }
    return config.layout.themes.empty() ? nullptr : &config.layout.themes.front();
}

}  // namespace

bool LoadLayoutGuideSheetConfigText(std::string_view configText, LayoutGuideSheetConfig& guideSheet) {
    guideSheet = {};
    LayoutGuideSheetParseContext context{&guideSheet};
    ForEachConfigEntry(configText, &context, &ApplyLayoutGuideSheetConfigEntry);
    return true;
}

void ResolveLayoutGuideSheetColors(const AppConfig& config, LayoutGuideSheetConfig& guideSheet) {
    const ThemeConfig* activeTheme = FindActiveTheme(config);
    if (activeTheme == nullptr) {
        return;
    }

    const RuntimeConfigSectionDescriptor* colorsSection = FindRuntimeConfigSection("colors");
    if (colorsSection == nullptr) {
        return;
    }

    const auto guideSheetLookup = [&config, activeTheme, colorsSection](
                                      std::string_view name) -> std::optional<ColorConfig> {
        if (std::optional<ColorConfig> themeColor = FindThemeColorToken(*activeTheme, name); themeColor.has_value()) {
            return themeColor;
        }
        return FindConfigColorFieldByKey(RuntimeConfigFields(*colorsSection), &config.layout.colors, name);
    };
    ResolveConfigColorFieldsInPlace(LayoutGuideSheetRuntimeConfigFields(), &guideSheet, guideSheetLookup);
}

bool SaveLayoutGuideSheetPng(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    const LayoutGuideSheetConfig& guideSheet,
    double scale,
    Trace& trace,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    LayoutGuideSheetPipelineStats collectedStats;
    LayoutGuideSheetPipelineStats* outputStats = stats != nullptr ? stats : &collectedStats;
    *outputStats = {};
    const std::string imagePathText = imagePath.string();
    trace.WriteFmt(TracePrefix::Diagnostics,
        RES_STR("layout_guide_sheet start path=\"%s\" layout=\"%s\""),
        imagePathText.c_str(),
        config.display.layout.c_str());

    DashboardRenderer renderer(trace);
    renderer.SetRenderScale(scale);
    renderer.SetConfig(config);
    renderer.SetRenderMode(DashboardRenderer::RenderMode::Normal);
    if (!renderer.Initialize()) {
        const std::string error = renderer.LastError();
        if (errorText != nullptr) {
            *errorText = error;
        }
        WriteRendererErrorTrace(trace, RES_STR("layout_guide_sheet_initialize"), error);
        trace.Write(TracePrefix::Diagnostics, RES_STR("layout_guide_sheet failed stage=\"initialize\""));
        return false;
    }

    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    std::vector<std::string> selectedCardIds;
    std::string localError;
    std::string* outputErrorText = errorText != nullptr ? errorText : &localError;
    if (!BuildLayoutGuideSheetPipelineInputs(
            renderer, snapshot, guideSheet, callouts, selectedCardIds, outputErrorText, outputStats)) {
        WriteRendererErrorTrace(trace, RES_STR("layout_guide_sheet_active_regions"), *outputErrorText);
        trace.Write(TracePrefix::Diagnostics, RES_STR("layout_guide_sheet failed stage=\"active_regions\""));
        return false;
    }

    std::vector<std::string> traceDetails;
    LayoutGuideSheetRenderStats renderStats;
    LayoutGuideSheetRenderer sheetRenderer(renderer, guideSheet);
    const bool saved = sheetRenderer.SavePng(
        imagePath, snapshot, callouts, selectedCardIds, &traceDetails, outputErrorText, &renderStats);
    RecordRenderStats(renderStats, outputStats);
    if (!saved) {
        WriteRendererErrorTrace(trace, RES_STR("layout_guide_sheet_save"), *outputErrorText);
        trace.Write(TracePrefix::Diagnostics, RES_STR("layout_guide_sheet failed stage=\"save\""));
        return false;
    }

    outputStats->traceDetails = traceDetails;
    for (const std::string& detail : outputStats->traceDetails) {
        trace.WriteFmt(TracePrefix::Diagnostics, RES_STR("layout_guide_sheet detail %s"), detail.c_str());
    }
    WritePipelineStatsTrace(trace, *outputStats);
    trace.WriteFmt(TracePrefix::Diagnostics, RES_STR("layout_guide_sheet end path=\"%s\""), imagePathText.c_str());
    return true;
}

bool RenderLayoutGuideSheetOffscreen(DashboardRenderer& renderer,
    const SystemSnapshot& snapshot,
    const LayoutGuideSheetConfig& guideSheet,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    if (stats != nullptr) {
        *stats = {};
    }

    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    std::vector<std::string> selectedCardIds;
    if (!BuildLayoutGuideSheetPipelineInputs(
            renderer, snapshot, guideSheet, callouts, selectedCardIds, errorText, stats)) {
        return false;
    }

    std::vector<std::string> traceDetails;
    LayoutGuideSheetRenderStats renderStats;
    LayoutGuideSheetRenderer sheetRenderer(renderer, guideSheet);
    const bool rendered =
        sheetRenderer.RenderOffscreen(snapshot, callouts, selectedCardIds, &traceDetails, errorText, &renderStats);
    RecordRenderStats(renderStats, stats);
    if (stats != nullptr) {
        stats->traceDetails = std::move(traceDetails);
    }
    return rendered;
}
