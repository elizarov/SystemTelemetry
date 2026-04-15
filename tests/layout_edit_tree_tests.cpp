#include <gtest/gtest.h>

#include "layout_edit_tree.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string ReadTemplateText() {
    const std::filesystem::path path = std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

LayoutNodeConfig MakeWidgetNode(const std::string& name) {
    LayoutNodeConfig node;
    node.name = name;
    return node;
}

LayoutNodeConfig MakeDashboardCardNode(const std::string& id) {
    LayoutNodeConfig node;
    node.name = id;
    return node;
}

LayoutNodeConfig MakeCardRefNode(const std::string& id) {
    LayoutNodeConfig node;
    node.name = id;
    node.cardReference = true;
    return node;
}

LayoutNodeConfig MakeContainerNode(const std::string& name, std::initializer_list<LayoutNodeConfig> children) {
    LayoutNodeConfig node;
    node.name = name;
    node.children.assign(children.begin(), children.end());
    return node;
}

LayoutCardConfig MakeCard(const std::string& id, const LayoutNodeConfig& layout) {
    LayoutCardConfig card;
    card.id = id;
    card.layout = layout;
    return card;
}

const LayoutEditTreeNode* FindRootNode(const LayoutEditTreeModel& model, const std::string& label) {
    const auto it = std::find_if(
        model.roots.begin(), model.roots.end(), [&](const LayoutEditTreeNode& node) { return node.label == label; });
    return it != model.roots.end() ? &(*it) : nullptr;
}

std::vector<std::string> RootLabels(const LayoutEditTreeModel& model) {
    std::vector<std::string> labels;
    for (const auto& node : model.roots) {
        labels.push_back(node.label);
    }
    return labels;
}

std::vector<std::string> ChildLabels(const LayoutEditTreeNode& node) {
    std::vector<std::string> labels;
    for (const auto& child : node.children) {
        labels.push_back(child.label);
    }
    return labels;
}

AppConfig MakeBaseConfig() {
    AppConfig config;
    config.display.layout = "primary";
    config.layout.structure.cardsLayout =
        MakeContainerNode("columns", {MakeDashboardCardNode("alpha"), MakeDashboardCardNode("beta")});
    config.layout.cards.push_back(
        MakeCard("alpha", MakeContainerNode("columns", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})));
    config.layout.cards.push_back(
        MakeCard("beta", MakeContainerNode("rows", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})));
    return config;
}

}  // namespace

TEST(LayoutEditTree, PreservesTemplateSectionAndFieldOrderForEditableSections) {
    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(MakeBaseConfig(), ReadTemplateText());

    EXPECT_EQ(RootLabels(model),
        (std::vector<std::string>{"metric_list",
            "drive_usage_list",
            "throughput",
            "gauge",
            "text",
            "network_footer",
            "layout.primary",
            "dashboard",
            "card_style",
            "fonts",
            "card.alpha",
            "card.beta"}));

    const LayoutEditTreeNode* metricList = FindRootNode(model, "metric_list");
    ASSERT_NE(metricList, nullptr);
    EXPECT_EQ(ChildLabels(*metricList), (std::vector<std::string>{"label_width", "bar_height", "row_gap"}));

    EXPECT_EQ(FindRootNode(model, "display"), nullptr);
    EXPECT_EQ(FindRootNode(model, "colors"), nullptr);
    EXPECT_EQ(FindRootNode(model, "layout_editor"), nullptr);
}

TEST(LayoutEditTree, IncludesOnlyTheActiveLayoutSection) {
    AppConfig config = MakeBaseConfig();
    config.display.layout = "secondary";

    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(config, ReadTemplateText());

    EXPECT_NE(FindRootNode(model, "layout.secondary"), nullptr);
    EXPECT_EQ(FindRootNode(model, "layout.primary"), nullptr);
}

TEST(LayoutEditTree, IncludesOnlyReachableCardsInEncounterOrderAndSkipsCycles) {
    AppConfig config;
    config.display.layout = "primary";
    config.layout.structure.cardsLayout =
        MakeContainerNode("columns", {MakeDashboardCardNode("alpha"), MakeDashboardCardNode("gamma")});
    config.layout.cards.push_back(
        MakeCard("alpha", MakeContainerNode("columns", {MakeCardRefNode("beta"), MakeWidgetNode("metric_list")})));
    config.layout.cards.push_back(
        MakeCard("beta", MakeContainerNode("columns", {MakeCardRefNode("alpha"), MakeWidgetNode("throughput")})));
    config.layout.cards.push_back(
        MakeCard("gamma", MakeContainerNode("columns", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})));
    config.layout.cards.push_back(
        MakeCard("delta", MakeContainerNode("columns", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})));

    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(config, ReadTemplateText());

    std::vector<std::string> cardRoots;
    for (const auto& label : RootLabels(model)) {
        if (label.rfind("card.", 0) == 0) {
            cardRoots.push_back(label);
        }
    }

    EXPECT_EQ(cardRoots, (std::vector<std::string>{"card.alpha", "card.beta", "card.gamma"}));
}

TEST(LayoutEditTree, BuildsLayoutAndCardSubtreesFromNestedContainers) {
    AppConfig config;
    config.display.layout = "primary";
    config.layout.structure.cardsLayout = MakeContainerNode("rows",
        {MakeContainerNode("columns", {MakeDashboardCardNode("alpha"), MakeDashboardCardNode("beta")}),
            MakeDashboardCardNode("gamma")});
    config.layout.cards.push_back(MakeCard("alpha",
        MakeContainerNode("rows",
            {MakeContainerNode("columns", {MakeWidgetNode("metric_list"), MakeWidgetNode("gauge")}),
                MakeWidgetNode("throughput")})));
    config.layout.cards.push_back(
        MakeCard("beta", MakeContainerNode("columns", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})));
    config.layout.cards.push_back(
        MakeCard("gamma", MakeContainerNode("columns", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})));

    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(config, ReadTemplateText());

    const LayoutEditTreeNode* layoutRoot = FindRootNode(model, "layout.primary");
    ASSERT_NE(layoutRoot, nullptr);
    ASSERT_EQ(layoutRoot->children.size(), 1u);
    EXPECT_EQ(layoutRoot->children[0].label, "cards");
    EXPECT_EQ(ChildLabels(layoutRoot->children[0]), (std::vector<std::string>{"alpha, beta", "columns, gamma"}));

    const LayoutEditTreeNode* alphaRoot = FindRootNode(model, "card.alpha");
    ASSERT_NE(alphaRoot, nullptr);
    ASSERT_EQ(alphaRoot->children.size(), 1u);
    EXPECT_EQ(alphaRoot->children[0].label, "layout");
    EXPECT_EQ(
        ChildLabels(alphaRoot->children[0]), (std::vector<std::string>{"metric_list, gauge", "columns, throughput"}));
}

TEST(LayoutEditTree, WeightLabelsAndFocusLookupResolveParameterAndWeightLeaves) {
    AppConfig config = MakeBaseConfig();

    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(config, ReadTemplateText());

    const LayoutEditTreeLeaf* parameterLeaf =
        FindLayoutEditTreeLeaf(model, LayoutEditFocusKey{LayoutEditParameter::GaugeRingThickness});
    ASSERT_NE(parameterLeaf, nullptr);
    EXPECT_EQ(parameterLeaf->sectionName, "gauge");
    EXPECT_EQ(parameterLeaf->memberName, "ring_thickness");

    const LayoutEditTreeLeaf* weightLeaf =
        FindLayoutEditTreeLeaf(model, LayoutEditFocusKey{LayoutWeightEditKey{"", {}, 0}});
    ASSERT_NE(weightLeaf, nullptr);
    EXPECT_EQ(weightLeaf->sectionName, "layout.primary");
    EXPECT_EQ(weightLeaf->memberName, "cards");
    EXPECT_EQ(weightLeaf->firstWeightName, "alpha");
    EXPECT_EQ(weightLeaf->secondWeightName, "beta");
    EXPECT_EQ(weightLeaf->weightAxis, LayoutGuideAxis::Vertical);
}

TEST(LayoutEditTree, ShowsReachableCardSectionsForRuntimeStyleDashboardCardNodes) {
    AppConfig config = MakeBaseConfig();

    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(config, ReadTemplateText());

    EXPECT_NE(FindRootNode(model, "card.alpha"), nullptr);
    EXPECT_NE(FindRootNode(model, "card.beta"), nullptr);
}

TEST(LayoutEditTree, CollapsesSingleChildContainerPathsInCardTrees) {
    AppConfig config;
    config.display.layout = "primary";
    config.layout.structure.cardsLayout = MakeContainerNode("columns", {MakeDashboardCardNode("gpu")});
    config.layout.cards.push_back(MakeCard("gpu",
        MakeContainerNode(
            "rows", {MakeContainerNode("columns", {MakeWidgetNode("gauge"), MakeWidgetNode("metric_list")})})));

    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(config, ReadTemplateText());

    const LayoutEditTreeNode* gpuRoot = FindRootNode(model, "card.gpu");
    ASSERT_NE(gpuRoot, nullptr);
    ASSERT_EQ(gpuRoot->children.size(), 1u);
    EXPECT_EQ(gpuRoot->children[0].label, "layout");
    ASSERT_EQ(gpuRoot->children[0].children.size(), 1u);
    EXPECT_EQ(gpuRoot->children[0].children[0].label, "gauge, metric_list");
    EXPECT_TRUE(gpuRoot->children[0].children[0].leaf.has_value());
}
