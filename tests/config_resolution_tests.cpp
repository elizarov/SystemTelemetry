#include <gtest/gtest.h>

#include "config/config_resolution.h"

namespace {

LayoutNodeConfig MakeWidgetNode(const std::string& name, const std::string& parameter = {}) {
    LayoutNodeConfig node;
    node.name = name;
    node.parameter = parameter;
    return node;
}

LayoutNodeConfig MakeContainerNode(const std::string& name, std::initializer_list<LayoutNodeConfig> children) {
    LayoutNodeConfig node;
    node.name = name;
    node.children.assign(children.begin(), children.end());
    return node;
}

LayoutSectionConfig MakeNamedLayout(const std::string& name, int width, int height, const std::string& rootName) {
    LayoutSectionConfig layout;
    layout.name = name;
    layout.window.width = width;
    layout.window.height = height;
    layout.cardsLayout.name = rootName;
    return layout;
}

}  // namespace

TEST(ConfigResolution, CollectsUniqueBoardBindingsFromNestedCardLayouts) {
    LayoutConfig layout;
    LayoutCardConfig card;
    card.id = "cpu";
    card.layout = MakeContainerNode("rows",
        {MakeWidgetNode("metric_list", "board.temp.cpu, gpu.load, board.fan.system"),
            MakeContainerNode("columns",
                {MakeWidgetNode("gauge", "board.temp.cpu"),
                    MakeWidgetNode("text", "board.fan.system"),
                    MakeWidgetNode("text", "board.temp.vrm"),
                    MakeWidgetNode("metric_list", "board.temp.cpu=CPU Legacy")})});
    layout.cards.push_back(card);

    const LayoutBindingSelection selection = CollectLayoutBindings(layout);

    ASSERT_EQ(selection.boardTemperatureNames.size(), 2u);
    EXPECT_EQ(selection.boardTemperatureNames[0], "cpu");
    EXPECT_EQ(selection.boardTemperatureNames[1], "vrm");
    ASSERT_EQ(selection.boardFanNames.size(), 1u);
    EXPECT_EQ(selection.boardFanNames[0], "system");
}

TEST(ConfigResolution, NormalizesConfiguredDrivesAndRemovesDuplicates) {
    const std::vector<std::string> drives = NormalizeConfiguredDrives({" c", "D:", "c\\", "", "1", "d"});

    ASSERT_EQ(drives.size(), 2u);
    EXPECT_EQ(drives[0], "C");
    EXPECT_EQ(drives[1], "D");
}

TEST(ConfigResolution, SelectsRequestedLayoutAndFallsBackToFirstLayout) {
    AppConfig config;
    config.layout.layouts.push_back(MakeNamedLayout("primary", 800, 480, "rows"));
    config.layout.layouts.push_back(MakeNamedLayout("secondary", 1024, 600, "columns"));

    ASSERT_TRUE(SelectResolvedLayout(config, "secondary"));
    EXPECT_EQ(config.display.layout, "secondary");
    EXPECT_EQ(config.layout.structure.window.width, 1024);
    EXPECT_EQ(config.layout.structure.window.height, 600);
    EXPECT_EQ(config.layout.structure.cardsLayout.name, "columns");

    config.display.layout.clear();
    ASSERT_TRUE(SelectResolvedLayout(config, "missing"));
    EXPECT_EQ(config.display.layout, "primary");
    EXPECT_EQ(config.layout.structure.window.width, 800);
    EXPECT_EQ(config.layout.structure.window.height, 480);
    EXPECT_EQ(config.layout.structure.cardsLayout.name, "rows");
}

TEST(ConfigResolution, ExtractTelemetrySettingsIncludesOnlyBoardAndSelectionInputs) {
    AppConfig config;
    config.network.adapterName = "Ethernet";
    config.storage.drives = {"C", "D"};
    config.layout.board.requestedTemperatureNames = {"cpu"};
    config.layout.board.requestedFanNames = {"system"};
    config.layout.board.temperatureSensorNames["cpu"] = "CPU";
    config.layout.board.fanSensorNames["system"] = "SYS_FAN";
    config.layout.gauge.labelBottom = 42;
    config.layout.metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.temp", MetricDisplayStyle::Scalar, false, 100.0, "C", "Core Temp"});

    const TelemetrySettings settings = ExtractTelemetrySettings(config);

    EXPECT_EQ(settings.selection.preferredAdapterName, "Ethernet");
    EXPECT_EQ(settings.selection.configuredDrives, (std::vector<std::string>{"C", "D"}));
    EXPECT_EQ(settings.board.requestedTemperatureNames, (std::vector<std::string>{"cpu"}));
    EXPECT_EQ(settings.board.requestedFanNames, (std::vector<std::string>{"system"}));
    EXPECT_EQ(settings.board.temperatureSensorNames.at("cpu"), "CPU");
    EXPECT_EQ(settings.board.fanSensorNames.at("system"), "SYS_FAN");
}

TEST(ConfigResolution, EffectiveRuntimeConfigPreservesUiEditsWhileOverlayingResolvedSelections) {
    AppConfig uiConfig;
    uiConfig.network.adapterName = "Configured Ethernet";
    uiConfig.storage.drives = {"Z"};
    uiConfig.layout.gauge.labelBottom = 42;
    uiConfig.layout.metrics.definitions.push_back(
        MetricDefinitionConfig{"gpu.temp", MetricDisplayStyle::Scalar, false, 100.0, "C", "Core Temp"});

    ResolvedTelemetrySelections resolvedSelections;
    resolvedSelections.adapterName = "Resolved Ethernet";
    resolvedSelections.drives = {"C", "D"};

    const AppConfig effectiveConfig = BuildEffectiveRuntimeConfig(uiConfig, resolvedSelections);
    const MetricDefinitionConfig* metric = FindMetricDefinition(effectiveConfig.layout.metrics, "gpu.temp");

    EXPECT_EQ(effectiveConfig.network.adapterName, "Resolved Ethernet");
    EXPECT_EQ(effectiveConfig.storage.drives, (std::vector<std::string>{"C", "D"}));
    EXPECT_EQ(effectiveConfig.layout.gauge.labelBottom, 42);
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->label, "Core Temp");
}
