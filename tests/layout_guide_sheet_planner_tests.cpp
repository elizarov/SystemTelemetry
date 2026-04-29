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
#include "layout_guide_sheet/layout_guide_sheet_layout.h"
#include "layout_guide_sheet/layout_guide_sheet_planner.h"
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
    std::vector<LayoutGuideSheetCardSummary> cards;
};

BuiltInLayoutGuideSheetContext BuildBuiltInLayoutGuideSheetContext() {
    LoadTestLocalizationCatalog();
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    EXPECT_TRUE(SelectLayout(config, "5x3"));

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

    return BuiltInLayoutGuideSheetContext{
        config, renderer.CollectLayoutEditActiveRegions(overlayState), renderer.CollectLayoutGuideSheetCardSummaries()};
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
        if (callout.parameterLine.rfind("[colors]", 0) != 0) {
            const bool hasHoverState = callout.hoverAnchorKey.has_value() || callout.hoverWidgetGuide.has_value() ||
                                       callout.hoverLayoutGuide.has_value() || callout.hoverGapAnchorKey.has_value();
            EXPECT_TRUE(hasHoverState) << callout.parameterLine;
        }
    }

    EXPECT_GT(actualTexts.size(), 20u);
    EXPECT_EQ(metricDefinitionCallouts, 1u);
}

TEST(LayoutGuideSheetPlanner, OverviewCalloutsUseDashboardAndCardChromeTargets) {
    const BuiltInLayoutGuideSheetContext context = BuildBuiltInLayoutGuideSheetContext();

    const std::vector<LayoutGuideSheetCalloutRequest> callouts =
        BuildLayoutGuideSheetOverviewCallouts(context.config, context.regions, context.cards);

    std::set<std::string> actualTexts;
    bool hasDashboardOuterMargin = false;
    bool hasLayoutCards = false;
    bool hasCardTitle = false;
    bool hasCardRadius = false;
    bool hasCardIconSize = false;
    for (const LayoutGuideSheetCalloutRequest& callout : callouts) {
        EXPECT_EQ(callout.sourceCardId, kLayoutGuideSheetOverviewSourceId);
        actualTexts.insert(callout.parameterLine + "\n" + callout.descriptionLine);
        hasDashboardOuterMargin |= callout.parameterLine.find("[dashboard] outer_margin") != std::string::npos;
        hasLayoutCards |= callout.parameterLine.find("[layout.5x3] cards") != std::string::npos;
        hasCardTitle |= callout.key == "card_title";
        hasCardRadius |= callout.parameterLine.find("[card_style] card_radius") != std::string::npos;
        hasCardIconSize |= callout.parameterLine.find("[card_style] header_icon_size") != std::string::npos;
    }

    EXPECT_GT(actualTexts.size(), 8u);
    EXPECT_TRUE(hasDashboardOuterMargin);
    EXPECT_TRUE(hasLayoutCards);
    EXPECT_TRUE(hasCardTitle);
    EXPECT_TRUE(hasCardRadius);
    EXPECT_TRUE(hasCardIconSize);
}

TEST(LayoutGuideSheetPlanner, CalloutGeometryUsesOnlyLeftAndRightSides) {
    const RenderRect card{100, 100, 300, 300};
    const std::vector<LayoutGuideSheetCalloutGeometryInput> inputs{
        {card, RenderRect{110, 180, 130, 200}},
        {card, RenderRect{270, 180, 290, 200}},
        {card, RenderRect{180, 110, 200, 130}},
        {card, RenderRect{180, 270, 200, 290}},
    };

    const std::vector<LayoutGuideSheetCalloutGeometry> planned = PlanLayoutGuideSheetCalloutGeometry(inputs);

    std::set<LayoutGuideSheetCalloutSide> sides;
    for (const LayoutGuideSheetCalloutGeometry& geometry : planned) {
        sides.insert(geometry.side);
    }
    EXPECT_EQ(sides.size(), 2u);
    EXPECT_TRUE(sides.contains(LayoutGuideSheetCalloutSide::Left));
    EXPECT_TRUE(sides.contains(LayoutGuideSheetCalloutSide::Right));
    EXPECT_EQ(planned[0].side, LayoutGuideSheetCalloutSide::Left);
    EXPECT_EQ(planned[1].side, LayoutGuideSheetCalloutSide::Right);
}
