#include <gtest/gtest.h>

#include "layout_edit_tree.h"
#include "localization_catalog.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <vector>

namespace {

std::string ReadTemplateText() {
    const std::filesystem::path path = std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

LocalizationCatalogMap ReadLocalizationCatalog() {
    const std::filesystem::path path =
        std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "localization.ini";
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return ParseLocalizationCatalog(buffer.str());
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

void ExpectSpecialSelectionHighlight(
    const LayoutEditTreeNode* node, LayoutEditSelectionHighlightSpecial expectedHighlight) {
    ASSERT_NE(node, nullptr);
    ASSERT_TRUE(node->selectionHighlight.has_value());
    ASSERT_TRUE(std::holds_alternative<LayoutEditSelectionHighlightSpecial>(*node->selectionHighlight));
    EXPECT_EQ(std::get<LayoutEditSelectionHighlightSpecial>(*node->selectionHighlight), expectedHighlight);
}

AppConfig MakeBaseConfig() {
    AppConfig config;
    config.display.layout = "primary";
    config.metrics.definitions = {
        MetricDefinitionConfig{"cpu.load", MetricDisplayStyle::Percent, true, 0.0, "%", "Load"},
        MetricDefinitionConfig{"gpu.temp", MetricDisplayStyle::Scalar, false, 100.0, "C", "Temp"},
        MetricDefinitionConfig{"drive.free", MetricDisplayStyle::SizeAuto, true, 0.0, "GB|TB", "Free"},
    };
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
        (std::vector<std::string>{"metrics",
            "metric_list",
            "drive_usage_list",
            "throughput",
            "gauge",
            "text",
            "network_footer",
            "dashboard",
            "card_style",
            "colors",
            "fonts",
            "layout.primary",
            "card.alpha",
            "card.beta"}));

    const LayoutEditTreeNode* metrics = FindRootNode(model, "metrics");
    ASSERT_NE(metrics, nullptr);
    ASSERT_FALSE(metrics->children.empty());
    EXPECT_EQ(metrics->children.front().label, "cpu.load");
    EXPECT_EQ(metrics->children.back().label, "drive.free");

    const LayoutEditTreeNode* metricList = FindRootNode(model, "metric_list");
    ASSERT_NE(metricList, nullptr);
    EXPECT_EQ(ChildLabels(*metricList), (std::vector<std::string>{"label_width", "bar_height", "row_gap"}));

    const LayoutEditTreeNode* colors = FindRootNode(model, "colors");
    ASSERT_NE(colors, nullptr);
    EXPECT_EQ(ChildLabels(*colors),
        (std::vector<std::string>{"background_color",
            "foreground_color",
            "icon_color",
            "accent_color",
            "layout_guide_color",
            "active_edit_color",
            "panel_border_color",
            "muted_text_color",
            "track_color",
            "panel_fill_color",
            "graph_background_color",
            "graph_axis_color",
            "graph_marker_color"}));

    EXPECT_EQ(FindRootNode(model, "display"), nullptr);
    EXPECT_NE(FindRootNode(model, "colors"), nullptr);
    EXPECT_EQ(FindRootNode(model, "layout_editor"), nullptr);

    const LayoutEditTreeNode* gaugeRoot = FindRootNode(model, "gauge");
    ASSERT_NE(gaugeRoot, nullptr);
    ASSERT_TRUE(gaugeRoot->selectionHighlight.has_value());
    ASSERT_TRUE(std::holds_alternative<DashboardWidgetClass>(*gaugeRoot->selectionHighlight));
    EXPECT_EQ(std::get<DashboardWidgetClass>(*gaugeRoot->selectionHighlight), DashboardWidgetClass::Gauge);

    ExpectSpecialSelectionHighlight(
        FindRootNode(model, "dashboard"), LayoutEditSelectionHighlightSpecial::DashboardBounds);
    ExpectSpecialSelectionHighlight(FindRootNode(model, "card_style"), LayoutEditSelectionHighlightSpecial::AllCards);
    ExpectSpecialSelectionHighlight(FindRootNode(model, "fonts"), LayoutEditSelectionHighlightSpecial::AllTexts);
    ExpectSpecialSelectionHighlight(
        FindRootNode(model, "layout.primary"), LayoutEditSelectionHighlightSpecial::DashboardBounds);

    const LayoutEditTreeNode* alphaRoot = FindRootNode(model, "card.alpha");
    ASSERT_NE(alphaRoot, nullptr);
    ASSERT_TRUE(alphaRoot->selectionHighlight.has_value());
    ASSERT_TRUE(std::holds_alternative<LayoutEditWidgetIdentity>(*alphaRoot->selectionHighlight));
    const auto& alphaHighlight = std::get<LayoutEditWidgetIdentity>(*alphaRoot->selectionHighlight);
    EXPECT_EQ(alphaHighlight.renderCardId, "alpha");
    EXPECT_EQ(alphaHighlight.editCardId, "alpha");
    EXPECT_TRUE(alphaHighlight.nodePath.empty());
    EXPECT_EQ(alphaHighlight.kind, LayoutEditWidgetIdentity::Kind::CardChrome);
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
            {MakeContainerNode(
                 "columns", {MakeWidgetNode("metric_list"), MakeWidgetNode("gauge"), MakeWidgetNode("text")}),
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
    ASSERT_TRUE(layoutRoot->children[0].selectionHighlight.has_value());
    ASSERT_TRUE(
        std::holds_alternative<LayoutEditSelectionHighlightSpecial>(*layoutRoot->children[0].selectionHighlight));
    EXPECT_EQ(std::get<LayoutEditSelectionHighlightSpecial>(*layoutRoot->children[0].selectionHighlight),
        LayoutEditSelectionHighlightSpecial::DashboardBounds);
    EXPECT_EQ(ChildLabels(layoutRoot->children[0]), (std::vector<std::string>{"alpha, beta", "columns, gamma"}));

    const LayoutEditTreeNode* alphaRoot = FindRootNode(model, "card.alpha");
    ASSERT_NE(alphaRoot, nullptr);
    ASSERT_EQ(alphaRoot->children.size(), 1u);
    EXPECT_EQ(alphaRoot->children[0].label, "layout");
    ASSERT_TRUE(alphaRoot->children[0].selectionHighlight.has_value());
    ASSERT_TRUE(std::holds_alternative<LayoutEditWidgetIdentity>(*alphaRoot->children[0].selectionHighlight));
    const auto& alphaRootHighlight = std::get<LayoutEditWidgetIdentity>(*alphaRoot->selectionHighlight);
    const auto& alphaGroupHighlight = std::get<LayoutEditWidgetIdentity>(*alphaRoot->children[0].selectionHighlight);
    EXPECT_EQ(alphaGroupHighlight.renderCardId, alphaRootHighlight.renderCardId);
    EXPECT_EQ(alphaGroupHighlight.editCardId, alphaRootHighlight.editCardId);
    EXPECT_EQ(alphaGroupHighlight.nodePath, alphaRootHighlight.nodePath);
    EXPECT_EQ(alphaGroupHighlight.kind, alphaRootHighlight.kind);
    EXPECT_EQ(ChildLabels(alphaRoot->children[0]), (std::vector<std::string>{"columns", "columns, throughput"}));

    const LayoutEditTreeNode* alphaContainer = &alphaRoot->children[0].children[0];
    ASSERT_TRUE(alphaContainer->selectionHighlight.has_value());
    ASSERT_TRUE(std::holds_alternative<LayoutContainerEditKey>(*alphaContainer->selectionHighlight));
    const auto& containerKey = std::get<LayoutContainerEditKey>(*alphaContainer->selectionHighlight);
    EXPECT_EQ(containerKey.editCardId, "alpha");
    EXPECT_EQ(containerKey.nodePath, (std::vector<size_t>{0}));
    EXPECT_EQ(ChildLabels(*alphaContainer), (std::vector<std::string>{"metric_list, gauge", "gauge, text"}));
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

    const LayoutEditTreeLeaf* metricLeaf =
        FindLayoutEditTreeLeaf(model, LayoutEditFocusKey{LayoutMetricEditKey{"gpu.temp"}});
    ASSERT_NE(metricLeaf, nullptr);
    EXPECT_EQ(metricLeaf->sectionName, "metrics");
    EXPECT_EQ(metricLeaf->memberName, "gpu.temp");
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

TEST(LayoutEditTree, EveryBuiltNodeHasLocalizedDescriptionAndLocationText) {
    const LayoutEditTreeModel model = BuildLayoutEditTreeModel(MakeBaseConfig(), ReadTemplateText());
    const LocalizationCatalogMap catalog = ReadLocalizationCatalog();

    std::function<void(const LayoutEditTreeNode&)> verifyNode = [&](const LayoutEditTreeNode& node) {
        EXPECT_FALSE(node.locationText.empty()) << node.label;
        EXPECT_FALSE(node.descriptionKey.empty()) << node.label;
        const auto it = catalog.find(node.descriptionKey);
        ASSERT_TRUE(it != catalog.end()) << "missing localization key: " << node.descriptionKey;
        EXPECT_FALSE(it->second.empty()) << "empty localization text for key: " << node.descriptionKey;
        for (const auto& child : node.children) {
            verifyNode(child);
        }
    };

    for (const auto& root : model.roots) {
        verifyNode(root);
    }
}
