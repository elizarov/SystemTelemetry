#include "layout_guide_sheet/layout_guide_sheet.h"

#include <chrono>
#include <string>

#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_planner.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_renderer.h"
#include "layout_model/dashboard_overlay_state.h"
#include "util/localization_catalog.h"
#include "util/trace.h"
#include "util/utf8.h"

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
    const std::vector<LayoutGuideSheetCardSummary> cards = renderer.CollectLayoutGuideSheetCardSummaries();
    const LayoutEditActiveRegions activeRegions = renderer.CollectLayoutEditActiveRegions(overlayState);
    LayoutGuideSheetRenderer sheetRenderer(renderer);
    const LayoutEditActiveRegions overviewActiveRegions = sheetRenderer.CollectOverviewActiveRegions(snapshot);
    RecordStats(&LayoutGuideSheetPipelineStats::activeRegions, activeRegionsStart, stats);

    InitializeLocalizationCatalog();
    const auto planStart = Clock::now();
    selectedCardIds = SelectLayoutGuideSheetCards(cards);
    const std::vector<LayoutGuideSheetCalloutRequest> overviewCallouts =
        BuildLayoutGuideSheetOverviewCallouts(renderer.Config(), overviewActiveRegions);
    const std::vector<LayoutGuideSheetCalloutRequest> cardCallouts =
        BuildLayoutGuideSheetCallouts(renderer.Config(), activeRegions, cards, selectedCardIds);
    callouts = MergeLayoutGuideSheetCallouts(overviewCallouts, cardCallouts);
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
    trace.Write("diagnostics:layout_guide_sheet stats selected_cards=" + std::to_string(stats.selectedCards) +
                " callouts=" + std::to_string(stats.callouts) + " " +
                Trace::FormatValueDouble("active_regions_ms", Milliseconds(stats.activeRegions)) + " " +
                Trace::FormatValueDouble("sheet_plan_ms", Milliseconds(stats.plan)) + " " +
                Trace::FormatValueDouble("sheet_measure_ms", Milliseconds(stats.measure)) + " " +
                Trace::FormatValueDouble("sheet_place_ms", Milliseconds(stats.placement)) + " " +
                Trace::FormatValueDouble("sheet_draw_ms", Milliseconds(stats.draw)));
}

}  // namespace

bool SaveLayoutGuideSheetPng(const std::filesystem::path& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    LayoutGuideSheetPipelineStats collectedStats;
    LayoutGuideSheetPipelineStats* outputStats = stats != nullptr ? stats : &collectedStats;
    *outputStats = {};
    trace.Write("diagnostics:layout_guide_sheet start path=\"" + Utf8FromWide(imagePath.wstring()) + "\" layout=\"" +
                config.display.layout + "\"");

    DashboardRenderer renderer(trace);
    renderer.SetRenderScale(scale);
    renderer.SetConfig(config);
    renderer.SetRenderMode(DashboardRenderer::RenderMode::Normal);
    if (!renderer.Initialize()) {
        if (errorText != nullptr) {
            *errorText = renderer.LastError();
        }
        trace.Write("diagnostics:layout_guide_sheet failed stage=\"initialize\"");
        return false;
    }

    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    std::vector<std::string> selectedCardIds;
    if (!BuildLayoutGuideSheetPipelineInputs(renderer, snapshot, callouts, selectedCardIds, errorText, outputStats)) {
        trace.Write("diagnostics:layout_guide_sheet failed stage=\"active_regions\"");
        return false;
    }

    std::vector<std::string> traceDetails;
    LayoutGuideSheetRenderStats renderStats;
    LayoutGuideSheetRenderer sheetRenderer(renderer);
    const bool saved =
        sheetRenderer.SavePng(imagePath, snapshot, callouts, selectedCardIds, &traceDetails, errorText, &renderStats);
    RecordRenderStats(renderStats, outputStats);
    if (!saved) {
        trace.Write("diagnostics:layout_guide_sheet failed stage=\"save\"");
        return false;
    }

    outputStats->traceDetails = traceDetails;
    for (const std::string& detail : outputStats->traceDetails) {
        trace.Write("diagnostics:layout_guide_sheet detail " + detail);
    }
    WritePipelineStatsTrace(trace, *outputStats);
    trace.Write("diagnostics:layout_guide_sheet end path=\"" + Utf8FromWide(imagePath.wstring()) + "\"");
    return true;
}

bool RenderLayoutGuideSheetOffscreen(DashboardRenderer& renderer,
    const SystemSnapshot& snapshot,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    if (stats != nullptr) {
        *stats = {};
    }

    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    std::vector<std::string> selectedCardIds;
    if (!BuildLayoutGuideSheetPipelineInputs(renderer, snapshot, callouts, selectedCardIds, errorText, stats)) {
        return false;
    }

    std::vector<std::string> traceDetails;
    LayoutGuideSheetRenderStats renderStats;
    LayoutGuideSheetRenderer sheetRenderer(renderer);
    const bool rendered =
        sheetRenderer.RenderOffscreen(snapshot, callouts, selectedCardIds, &traceDetails, errorText, &renderStats);
    RecordRenderStats(renderStats, stats);
    if (stats != nullptr) {
        stats->traceDetails = std::move(traceDetails);
    }
    return rendered;
}
