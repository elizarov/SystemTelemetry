#include "layout_guide_sheet/layout_guide_sheet.h"

#include <chrono>
#include <string>

#include "dashboard_renderer/dashboard_renderer.h"
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
        "layout_guide_sheet stats selected_cards=%zu callouts=%zu active_regions_ms=%s sheet_plan_ms=%s "
        "sheet_measure_ms=%s sheet_place_ms=%s sheet_draw_ms=%s",
        stats.selectedCards,
        stats.callouts,
        activeRegionsText.c_str(),
        planText.c_str(),
        measureText.c_str(),
        placementText.c_str(),
        drawText.c_str());
}

}  // namespace

bool SaveLayoutGuideSheetPng(const FilePath& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    LayoutGuideSheetPipelineStats collectedStats;
    LayoutGuideSheetPipelineStats* outputStats = stats != nullptr ? stats : &collectedStats;
    *outputStats = {};
    const std::string imagePathText = imagePath.string();
    trace.WriteFmt(TracePrefix::Diagnostics,
        "layout_guide_sheet start path=\"%s\" layout=\"%s\"",
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
        WriteRendererErrorTrace(trace, "layout_guide_sheet_initialize", error);
        trace.Write(TracePrefix::Diagnostics, "layout_guide_sheet failed stage=\"initialize\"");
        return false;
    }

    std::vector<LayoutGuideSheetCalloutRequest> callouts;
    std::vector<std::string> selectedCardIds;
    std::string localError;
    std::string* outputErrorText = errorText != nullptr ? errorText : &localError;
    if (!BuildLayoutGuideSheetPipelineInputs(
            renderer, snapshot, callouts, selectedCardIds, outputErrorText, outputStats)) {
        WriteRendererErrorTrace(trace, "layout_guide_sheet_active_regions", *outputErrorText);
        trace.Write(TracePrefix::Diagnostics, "layout_guide_sheet failed stage=\"active_regions\"");
        return false;
    }

    std::vector<std::string> traceDetails;
    LayoutGuideSheetRenderStats renderStats;
    LayoutGuideSheetRenderer sheetRenderer(renderer);
    const bool saved = sheetRenderer.SavePng(
        imagePath, snapshot, callouts, selectedCardIds, &traceDetails, outputErrorText, &renderStats);
    RecordRenderStats(renderStats, outputStats);
    if (!saved) {
        WriteRendererErrorTrace(trace, "layout_guide_sheet_save", *outputErrorText);
        trace.Write(TracePrefix::Diagnostics, "layout_guide_sheet failed stage=\"save\"");
        return false;
    }

    outputStats->traceDetails = traceDetails;
    for (const std::string& detail : outputStats->traceDetails) {
        trace.WriteFmt(TracePrefix::Diagnostics, "layout_guide_sheet detail %s", detail.c_str());
    }
    WritePipelineStatsTrace(trace, *outputStats);
    trace.WriteFmt(TracePrefix::Diagnostics, "layout_guide_sheet end path=\"%s\"", imagePathText.c_str());
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
