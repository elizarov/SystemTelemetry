#include <gtest/gtest.h>

#include "config_resolution.h"

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
    config.layouts.push_back(MakeNamedLayout("primary", 800, 480, "rows"));
    config.layouts.push_back(MakeNamedLayout("secondary", 1024, 600, "columns"));

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
