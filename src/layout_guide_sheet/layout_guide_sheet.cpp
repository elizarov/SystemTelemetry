#include "layout_guide_sheet/layout_guide_sheet.h"

#include <chrono>

#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_planner.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_renderer.h"
#include "layout_model/dashboard_overlay_state.h"
#include "util/localization_catalog.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using Clock = std::chrono::steady_clock;

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
    RecordStats(&LayoutGuideSheetPipelineStats::activeRegions, activeRegionsStart, stats);

    InitializeLocalizationCatalog();
    const auto planStart = Clock::now();
    selectedCardIds = SelectLayoutGuideSheetCards(cards);
    const std::vector<LayoutGuideSheetCalloutRequest> overviewCallouts =
        BuildLayoutGuideSheetOverviewCallouts(renderer.Config(), activeRegions, cards);
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

}  // namespace

bool SaveLayoutGuideSheetPng(const std::filesystem::path& imagePath,
    const SystemSnapshot& snapshot,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText,
    LayoutGuideSheetPipelineStats* stats) {
    if (stats != nullptr) {
        *stats = {};
    }
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
    if (!BuildLayoutGuideSheetPipelineInputs(renderer, snapshot, callouts, selectedCardIds, errorText, stats)) {
        trace.Write("diagnostics:layout_guide_sheet failed stage=\"active_regions\"");
        return false;
    }

    std::vector<std::string> traceDetails;
    LayoutGuideSheetRenderStats renderStats;
    LayoutGuideSheetRenderer sheetRenderer(renderer);
    const bool saved =
        sheetRenderer.SavePng(imagePath, snapshot, callouts, selectedCardIds, &traceDetails, errorText, &renderStats);
    RecordRenderStats(renderStats, stats);
    if (!saved) {
        trace.Write("diagnostics:layout_guide_sheet failed stage=\"save\"");
        return false;
    }

    if (stats != nullptr) {
        stats->traceDetails = traceDetails;
    }
    std::string endTrace = "diagnostics:layout_guide_sheet end path=\"" + Utf8FromWide(imagePath.wstring()) + "\"";
    for (const std::string& detail : traceDetails) {
        endTrace += " " + detail;
    }
    trace.Write(endTrace);
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
