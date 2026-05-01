#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "config/config_parser.h"
#include "config/config_resolution.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_placement.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_planner.h"
#include "layout_guide_sheet/impl/layout_guide_sheet_renderer.h"
#include "telemetry/impl/collector_fake.h"
#include "util/localization_catalog.h"
#include "util/trace.h"

namespace {

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
}

ConfigParseContext TestConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

void LoadTestLocalizationCatalog() {
    const std::filesystem::path path =
        std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "localization.ini";
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    ReplaceLocalizationCatalogForTesting(ParseLocalizationCatalog(buffer.str()));
}

struct BuiltInLayoutGuideSheetContext {
    AppConfig config;
    LayoutEditActiveRegions regions;
    LayoutEditActiveRegions overviewRegions;
    std::vector<LayoutGuideSheetCardSummary> cards;
};

BuiltInLayoutGuideSheetContext BuildBuiltInLayoutGuideSheetContext(const char* layoutName = "5x3") {
    LoadTestLocalizationCatalog();
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    EXPECT_TRUE(SelectLayout(config, layoutName));

    Trace trace;
    std::unique_ptr<TelemetryCollector> telemetry =
        CreateFakeTelemetryCollector(std::filesystem::current_path(), {}, nullptr, trace);
    EXPECT_NE(telemetry, nullptr);
    std::string telemetryError;
    EXPECT_TRUE(telemetry->Initialize(ExtractTelemetrySettings(config), &telemetryError)) << telemetryError;
    telemetry->UpdateSnapshot();

    DashboardRenderer renderer(trace);
    renderer.SetConfig(config);

    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;
    overlayState.forceLayoutEditAffordances = true;
    EXPECT_TRUE(renderer.RenderSnapshotOffscreen(telemetry->Snapshot(), overlayState)) << renderer.LastError();
    LayoutGuideSheetRenderer sheetRenderer(renderer);

    return BuiltInLayoutGuideSheetContext{config,
        renderer.CollectLayoutEditActiveRegions(overlayState),
        sheetRenderer.CollectOverviewActiveRegions(telemetry->Snapshot()),
        renderer.CollectLayoutGuideSheetCardSummaries()};
}

LayoutGuideSheetCardSummary TestCardSummary(std::string id, std::vector<WidgetClass> widgetClasses) {
    LayoutGuideSheetCardSummary summary;
    summary.id = std::move(id);
    summary.widgetClasses = std::move(widgetClasses);
    return summary;
}

}  // namespace

TEST(LayoutGuideSheetPlanner, CardSelectionUsesBestSetInsteadOfGreedyFirstMatch) {
    const std::vector<LayoutGuideSheetCardSummary> cards{
        TestCardSummary("cpu", {WidgetClass::Gauge, WidgetClass::MetricList}),
        TestCardSummary("network", {WidgetClass::Throughput, WidgetClass::NetworkFooter}),
        TestCardSummary("storage", {WidgetClass::Throughput, WidgetClass::NetworkFooter, WidgetClass::DriveUsageList}),
        TestCardSummary("time", {WidgetClass::ClockTime, WidgetClass::ClockDate}),
    };

    const std::vector<std::string> selected = SelectLayoutGuideSheetCards(cards);

    EXPECT_EQ(selected, (std::vector<std::string>{"cpu", "storage", "time"}));
}

TEST(LayoutGuideSheetPlanner, BuiltInCardSelectionDoesNotSelectNetworkWhenStorageCoversItsWidgets) {
    const BuiltInLayoutGuideSheetContext context = BuildBuiltInLayoutGuideSheetContext();

    const std::vector<std::string> selected = SelectLayoutGuideSheetCards(context.cards);

    EXPECT_EQ(selected, (std::vector<std::string>{"cpu", "storage", "time"}));
}

TEST(LayoutGuideSheetPlanner, CalloutSelectionUsesOnlySelectedCardsAndGroupsMetricDefinitions) {
    const BuiltInLayoutGuideSheetContext context = BuildBuiltInLayoutGuideSheetContext();

    const std::vector<std::string> selected = SelectLayoutGuideSheetCards(context.cards);
    const std::vector<LayoutGuideSheetCalloutRequest> callouts =
        BuildLayoutGuideSheetCallouts(context.config, context.regions, context.cards, selected);

    std::set<std::string> actualTexts;
    size_t metricDefinitionCallouts = 0;
    size_t cpuMetricListLayoutCallouts = 0;
    for (const LayoutGuideSheetCalloutRequest& callout : callouts) {
        actualTexts.insert(callout.parameterLine + "\n" + callout.descriptionLine);
        EXPECT_FALSE(callout.descriptionLine.empty()) << callout.parameterLine;
        EXPECT_TRUE(std::find(selected.begin(), selected.end(), callout.sourceCardId) != selected.end())
            << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("gpu."), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("[card.gpu]"), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("[card.network]"), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("[card.cpu] title"), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("[card_style]"), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("[layout.5x3]"), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("[dashboard]"), std::string::npos) << callout.parameterLine;
        if (callout.key == "metric_definition") {
            ++metricDefinitionCallouts;
            EXPECT_TRUE(callout.hoverAnchorKey.has_value());
            EXPECT_NE(callout.parameterLine.find("[metrics] cpu."), std::string::npos) << callout.parameterLine;
        }
        if (callout.parameterLine.find("[card.cpu] layout = metric_list(") != std::string::npos) {
            ++cpuMetricListLayoutCallouts;
            EXPECT_NE(callout.parameterLine.find("metric_list(cpu.ram)"), std::string::npos) << callout.parameterLine;
        }
        if (callout.parameterLine.rfind("[colors]", 0) != 0) {
            const bool hasHoverState = callout.hoverAnchorKey.has_value() || callout.hoverWidgetGuide.has_value() ||
                                       callout.hoverLayoutGuide.has_value() || callout.hoverGapAnchorKey.has_value();
            EXPECT_TRUE(hasHoverState) << callout.parameterLine;
        }
    }

    EXPECT_GT(actualTexts.size(), 20u);
    EXPECT_EQ(metricDefinitionCallouts, 1u);
    EXPECT_EQ(cpuMetricListLayoutCallouts, 1u);
}

TEST(LayoutGuideSheetPlanner, OverviewCalloutsUseDashboardAndCardChromeTargets) {
    const BuiltInLayoutGuideSheetContext context = BuildBuiltInLayoutGuideSheetContext();

    const std::vector<LayoutGuideSheetCalloutRequest> callouts =
        BuildLayoutGuideSheetOverviewCallouts(context.config, context.overviewRegions);

    std::set<std::string> actualTexts;
    bool hasDashboardOuterMargin = false;
    bool hasLayoutCards = false;
    bool hasCardTitle = false;
    bool hasCardRadius = false;
    bool hasCardIconSize = false;
    bool hasForegroundColor = false;
    bool hasIconColor = false;
    size_t horizontalReorderCallouts = 0;
    size_t verticalReorderCallouts = 0;
    size_t horizontalSizingGuides = 0;
    size_t verticalSizingGuides = 0;
    for (const LayoutGuideSheetCalloutRequest& callout : callouts) {
        EXPECT_EQ(callout.sourceCardId, kLayoutGuideSheetOverviewSourceId);
        actualTexts.insert(callout.parameterLine + "\n" + callout.descriptionLine);
        hasDashboardOuterMargin |= callout.parameterLine.find("[dashboard] outer_margin") != std::string::npos;
        hasLayoutCards |= callout.parameterLine.find("[layout.5x3] cards") != std::string::npos;
        hasCardTitle |= callout.key == "card_title";
        hasCardRadius |= callout.parameterLine.find("[card_style] card_radius") != std::string::npos;
        hasCardIconSize |= callout.parameterLine.find("[card_style] header_icon_size") != std::string::npos;
        hasForegroundColor |= callout.parameterLine.find("[colors] foreground_color") != std::string::npos &&
                              callout.hoverColorParameter == LayoutEditParameter::ColorForeground;
        hasIconColor |= callout.parameterLine.find("[colors] icon_color") != std::string::npos &&
                        callout.hoverColorParameter == LayoutEditParameter::ColorIcon;
        if (callout.key == "overview_horizontal_layout_reorder") {
            ++horizontalReorderCallouts;
            EXPECT_NE(callout.descriptionLine.find("left or right"), std::string::npos) << callout.descriptionLine;
            EXPECT_TRUE(callout.hoverAnchorKey.has_value());
        }
        if (callout.key == "overview_vertical_layout_reorder") {
            ++verticalReorderCallouts;
            EXPECT_NE(callout.descriptionLine.find("up or down"), std::string::npos) << callout.descriptionLine;
            EXPECT_TRUE(callout.hoverAnchorKey.has_value());
        }
        if (callout.hoverLayoutGuide.has_value() && callout.hoverLayoutGuide->renderCardId.empty()) {
            if (callout.hoverLayoutGuide->axis == LayoutGuideAxis::Horizontal) {
                ++horizontalSizingGuides;
            } else {
                ++verticalSizingGuides;
            }
        }
    }

    EXPECT_GT(actualTexts.size(), 8u);
    EXPECT_TRUE(hasDashboardOuterMargin);
    EXPECT_TRUE(hasLayoutCards);
    EXPECT_TRUE(hasCardTitle);
    EXPECT_TRUE(hasCardRadius);
    EXPECT_TRUE(hasCardIconSize);
    EXPECT_TRUE(hasForegroundColor);
    EXPECT_TRUE(hasIconColor);
    EXPECT_EQ(horizontalReorderCallouts, 1u);
    EXPECT_EQ(verticalReorderCallouts, 1u);
    EXPECT_EQ(horizontalSizingGuides, 1u);
    EXPECT_EQ(verticalSizingGuides, 1u);
}

TEST(LayoutGuideSheetPlanner, OverviewCalloutsDoNotUseWidgetColorTargets) {
    const BuiltInLayoutGuideSheetContext context = BuildBuiltInLayoutGuideSheetContext("3x5");

    const std::vector<LayoutGuideSheetCalloutRequest> callouts =
        BuildLayoutGuideSheetOverviewCallouts(context.config, context.overviewRegions);

    for (const LayoutGuideSheetCalloutRequest& callout : callouts) {
        if (!callout.hoverColorParameter.has_value()) {
            continue;
        }
        EXPECT_TRUE(*callout.hoverColorParameter == LayoutEditParameter::ColorForeground ||
                    *callout.hoverColorParameter == LayoutEditParameter::ColorIcon)
            << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("track_color"), std::string::npos) << callout.parameterLine;
        EXPECT_EQ(callout.parameterLine.find("peak_ghost_color"), std::string::npos) << callout.parameterLine;
    }
}

TEST(LayoutGuideSheetPlanner, MergedCalloutsDoNotRepeatOverviewColorParametersOnCards) {
    const BuiltInLayoutGuideSheetContext context = BuildBuiltInLayoutGuideSheetContext();

    const std::vector<std::string> selected = SelectLayoutGuideSheetCards(context.cards);
    const std::vector<LayoutGuideSheetCalloutRequest> overviewCallouts =
        BuildLayoutGuideSheetOverviewCallouts(context.config, context.overviewRegions);
    const std::vector<LayoutGuideSheetCalloutRequest> cardCallouts =
        BuildLayoutGuideSheetCallouts(context.config, context.regions, context.cards, selected);

    const std::vector<LayoutGuideSheetCalloutRequest> merged =
        MergeLayoutGuideSheetCallouts(overviewCallouts, cardCallouts);

    std::set<LayoutEditParameter> seenColorParameters;
    size_t iconColorCallouts = 0;
    for (const LayoutGuideSheetCalloutRequest& callout : merged) {
        if (!callout.hoverColorParameter.has_value()) {
            continue;
        }
        EXPECT_TRUE(seenColorParameters.insert(*callout.hoverColorParameter).second) << callout.parameterLine;
        if (*callout.hoverColorParameter == LayoutEditParameter::ColorIcon) {
            ++iconColorCallouts;
            EXPECT_EQ(callout.sourceCardId, kLayoutGuideSheetOverviewSourceId);
        }
    }

    EXPECT_EQ(iconColorCallouts, 1u);
}

TEST(LayoutGuideSheetPlanner, PlacementPromotesOuterSideItemsToTopAndBottom) {
    const RenderRect card{100, 100, 300, 300};
    std::vector<LayoutGuideSheetCardPlacement> cardPlacements{{"cpu", card, card, false}};
    std::vector<LayoutGuideSheetPlacementCallout> callouts;
    const auto addCallout = [&](std::string key, RenderRect target) {
        LayoutGuideSheetPlacementCallout callout;
        callout.key = std::move(key);
        callout.sourceCardId = "cpu";
        callout.targetRect = target;
        callout.bubbleRect = RenderRect{0, 0, 80, 30};
        callout.order = callouts.size();
        callouts.push_back(std::move(callout));
    };
    addCallout("left-middle", RenderRect{110, 180, 130, 200});
    addCallout("right-middle", RenderRect{270, 180, 290, 200});
    addCallout("center-top", RenderRect{180, 110, 200, 130});
    addCallout("center-bottom", RenderRect{180, 270, 200, 290});
    addCallout("left-bottom", RenderRect{120, 260, 140, 280});
    addCallout("right-top", RenderRect{260, 120, 280, 140});

    const LayoutGuideSheetPlacementResult result = PlaceLayoutGuideSheetCallouts(cardPlacements,
        callouts,
        LayoutGuideSheetPlacementStyle{10, 12, 4, 20, 0, 1},
        [](LayoutGuideSheetPlacementCallout&, int) {});

    std::set<LayoutGuideSheetExitSide> sides;
    for (const LayoutGuideSheetPlacementCallout& callout : callouts) {
        sides.insert(callout.exitSide);
    }
    ASSERT_EQ(result.blocks.size(), 1u);
    EXPECT_TRUE(result.remainingIntersections.empty());
    EXPECT_EQ(sides.size(), 4u);
    EXPECT_TRUE(sides.contains(LayoutGuideSheetExitSide::Left));
    EXPECT_TRUE(sides.contains(LayoutGuideSheetExitSide::Right));
    EXPECT_TRUE(sides.contains(LayoutGuideSheetExitSide::Top));
    EXPECT_TRUE(sides.contains(LayoutGuideSheetExitSide::Bottom));
    EXPECT_EQ(result.blocks.front().leftCallouts, 2u);
    EXPECT_EQ(result.blocks.front().rightCallouts, 2u);
    EXPECT_EQ(result.blocks.front().topCallouts, 1u);
    EXPECT_EQ(result.blocks.front().bottomCallouts, 1u);
}
