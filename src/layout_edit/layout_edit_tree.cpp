#include "layout_edit/layout_edit_tree.h"

#include "config/config_parser.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_model/layout_edit_helpers.h"
#include "layout_model/layout_edit_parameter_metadata.h"
#include "util/strings.h"
#include "util/text_format.h"

namespace {

enum class TemplateSectionKind {
    StaticSection,
    ThemeSectionSlot,
    LayoutSectionSlot,
    CardSectionSlot,
};

struct TemplateSectionSlot {
    TemplateSectionKind kind = TemplateSectionKind::StaticSection;
    std::string sectionName;
    std::vector<std::string> keys;
};

std::vector<TemplateSectionSlot> ParseTemplateSections(std::string_view text) {
    std::vector<TemplateSectionSlot> sections;
    TemplateSectionSlot* currentStaticSection = nullptr;
    bool themeSlotAdded = false;
    bool layoutSlotAdded = false;
    bool cardSlotAdded = false;

    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }

        const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
        if (!line.empty() && line[0] != ';' && line[0] != '#') {
            if (line.front() == '[' && line.back() == ']') {
                const std::string sectionName = Trim(std::string_view(line).substr(1, line.size() - 2));
                currentStaticSection = nullptr;
                if (sectionName.rfind("theme.", 0) == 0) {
                    if (!themeSlotAdded) {
                        sections.push_back({TemplateSectionKind::ThemeSectionSlot, "theme"});
                        themeSlotAdded = true;
                    }
                } else if (sectionName.rfind("layout.", 0) == 0) {
                    if (!layoutSlotAdded) {
                        sections.push_back({TemplateSectionKind::LayoutSectionSlot, "layout"});
                        layoutSlotAdded = true;
                    }
                } else if (sectionName.rfind("card.", 0) == 0) {
                    if (!cardSlotAdded) {
                        sections.push_back({TemplateSectionKind::CardSectionSlot, "card"});
                        cardSlotAdded = true;
                    }
                } else {
                    sections.push_back({TemplateSectionKind::StaticSection, sectionName});
                    currentStaticSection = &sections.back();
                }
            } else if (currentStaticSection != nullptr) {
                const size_t equals = line.find('=');
                if (equals != std::string::npos) {
                    currentStaticSection->keys.push_back(Trim(std::string_view(line).substr(0, equals)));
                }
            }
        }

        lineStart = lineEnd;
        while (lineStart < text.size() && (text[lineStart] == '\r' || text[lineStart] == '\n')) {
            ++lineStart;
        }
    }

    return sections;
}

std::string ChildDisplayName(const LayoutNodeConfig& node) {
    return node.name.empty() ? "unknown" : node.name;
}

std::string SectionLocationText(std::string_view sectionName) {
    return FormatText("[%.*s]", static_cast<int>(sectionName.size()), sectionName.data());
}

std::string MemberLocationText(std::string_view sectionName, std::string_view memberName) {
    return FormatText("[%.*s] %.*s",
        static_cast<int>(sectionName.size()),
        sectionName.data(),
        static_cast<int>(memberName.size()),
        memberName.data());
}

std::string ContainerLocationText(
    std::string_view sectionName, std::string_view memberName, std::string_view containerName) {
    return FormatText("[%.*s] %.*s %.*s(...)",
        static_cast<int>(sectionName.size()),
        sectionName.data(),
        static_cast<int>(memberName.size()),
        memberName.data(),
        static_cast<int>(containerName.size()),
        containerName.data());
}

std::optional<LayoutEditSelectionHighlight> SectionSelectionHighlight(std::string_view sectionName) {
    if (sectionName == "card_style") {
        return LayoutEditSelectionHighlight{LayoutEditSelectionHighlightSpecial::AllCards};
    }
    if (sectionName == "fonts") {
        return LayoutEditSelectionHighlight{LayoutEditSelectionHighlightSpecial::AllTexts};
    }
    if (sectionName == "dashboard") {
        return LayoutEditSelectionHighlight{LayoutEditSelectionHighlightSpecial::DashboardBounds};
    }
    const auto widgetClass = sectionName.empty() ? std::nullopt : EnumFromString<WidgetClass>(sectionName);
    if (!widgetClass.has_value()) {
        return std::nullopt;
    }
    return LayoutEditSelectionHighlight{*widgetClass};
}

std::string SectionDescriptionKey(std::string_view sectionName) {
    if (sectionName.rfind("layout.", 0) == 0) {
        return "layout_edit.section.layout";
    }
    if (sectionName.rfind("theme.", 0) == 0) {
        return "layout_edit.section.theme";
    }
    if (sectionName.rfind("card.", 0) == 0) {
        return "layout_edit.section.card";
    }
    return FormatText("layout_edit.section.%.*s", static_cast<int>(sectionName.size()), sectionName.data());
}

std::string GroupDescriptionKey(std::string_view memberName) {
    return FormatText("layout_edit.group.%.*s", static_cast<int>(memberName.size()), memberName.data());
}

std::string CardMemberDescriptionKey(std::string_view memberName) {
    return FormatText("config.card.%.*s", static_cast<int>(memberName.size()), memberName.data());
}

std::string ContainerDescriptionKey(std::string_view containerName) {
    return FormatText("layout_edit.container.%.*s", static_cast<int>(containerName.size()), containerName.data());
}

bool IsFixedHeightRowChild(const LayoutNodeConfig& node) {
    return node.name == "text" || node.name == "network_footer" || node.name == "vertical_spacer" ||
           node.name == "clock_time" || node.name == "clock_date";
}

bool IsVerticalSpringRowChild(const LayoutNodeConfig& node) {
    return node.name == "vertical_spring";
}

bool SeparatorIsEditable(const LayoutNodeConfig& node, size_t separatorIndex) {
    if (separatorIndex + 1 >= node.children.size()) {
        return false;
    }
    if (node.name != "rows") {
        return true;
    }
    return !IsFixedHeightRowChild(node.children[separatorIndex]) &&
           !IsVerticalSpringRowChild(node.children[separatorIndex]) &&
           !IsFixedHeightRowChild(node.children[separatorIndex + 1]) &&
           !IsVerticalSpringRowChild(node.children[separatorIndex + 1]);
}

const LayoutCardConfig* FindCardConfig(const LayoutConfig& layout, std::string_view cardId) {
    for (const LayoutCardConfig& card : layout.cards) {
        if (card.id == cardId) {
            return &card;
        }
    }
    return nullptr;
}

bool ContainsString(const std::vector<std::string>& values, const std::string& value) {
    for (const std::string& existing : values) {
        if (existing == value) {
            return true;
        }
    }
    return false;
}

void CollectReachableCardLayoutCards(const LayoutConfig& layout,
    const LayoutNodeConfig& node,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack);

void CollectReachableCardById(const LayoutConfig& layout,
    const std::string& cardId,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack) {
    if (ContainsString(recursionStack, cardId)) {
        return;
    }
    if (!ContainsString(orderedCards, cardId)) {
        orderedCards.push_back(cardId);
    }

    const LayoutCardConfig* card = FindCardConfig(layout, cardId);
    if (card == nullptr) {
        return;
    }

    recursionStack.push_back(cardId);
    CollectReachableCardLayoutCards(layout, card->layout, orderedCards, recursionStack);
    recursionStack.pop_back();
}

void CollectReachableDashboardCards(const LayoutConfig& layout,
    const LayoutNodeConfig& node,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack) {
    if (node.name == "rows" || node.name == "columns") {
        for (const auto& child : node.children) {
            CollectReachableDashboardCards(layout, child, orderedCards, recursionStack);
        }
        return;
    }

    if (!node.name.empty()) {
        CollectReachableCardById(layout, node.name, orderedCards, recursionStack);
    }
}

void CollectReachableCardLayoutCards(const LayoutConfig& layout,
    const LayoutNodeConfig& node,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack) {
    if (node.cardReference) {
        CollectReachableCardById(layout, node.name, orderedCards, recursionStack);
        return;
    }

    for (const auto& child : node.children) {
        CollectReachableCardLayoutCards(layout, child, orderedCards, recursionStack);
    }
}

std::vector<std::string> CollectReachableCards(const AppConfig& config) {
    std::vector<std::string> orderedCards;
    std::vector<std::string> recursionStack;
    CollectReachableDashboardCards(config.layout, config.layout.structure.cardsLayout, orderedCards, recursionStack);
    return orderedCards;
}

void CollectTopLevelCardsFromNode(const LayoutNodeConfig& node, std::vector<std::string>& orderedCards) {
    if (node.name == "rows" || node.name == "columns") {
        for (const auto& child : node.children) {
            CollectTopLevelCardsFromNode(child, orderedCards);
        }
        return;
    }
    if (!node.name.empty() && !ContainsString(orderedCards, node.name)) {
        orderedCards.push_back(node.name);
    }
}

std::vector<std::string> CollectTopLevelCards(const AppConfig& config) {
    std::vector<std::string> orderedCards;
    CollectTopLevelCardsFromNode(config.layout.structure.cardsLayout, orderedCards);
    return orderedCards;
}

void MoveLayoutEditTreeNode(LayoutEditTreeNode& target, LayoutEditTreeNode&& source) {
    // Size: avoid whole-node move assignment, which instantiates optional/variant assignment helpers.
    target.kind = source.kind;
    target.label = std::move(source.label);
    target.locationText = std::move(source.locationText);
    target.descriptionKey = std::move(source.descriptionKey);
    target.initiallyExpanded = source.initiallyExpanded;
    target.selectionHighlight.reset();
    if (source.selectionHighlight.has_value()) {
        target.selectionHighlight.emplace(std::move(*source.selectionHighlight));
    }
    target.leaf.reset();
    if (source.leaf.has_value()) {
        target.leaf.emplace(std::move(*source.leaf));
    }
    target.children = std::move(source.children);
}

bool BuildNodeFieldLeaf(const std::string& sectionName,
    const std::string& memberName,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    WidgetClass widgetClass,
    LayoutEditTreeNode& leafNode) {
    const auto key = LayoutNodeFieldEditKeyForWidgetParameter(editCardId, nodePath, widgetClass);
    if (!key.has_value()) {
        return false;
    }
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(*key);
    if (descriptor == nullptr) {
        return false;
    }

    leafNode.kind = LayoutEditTreeNodeKind::Leaf;
    leafNode.label = std::string(descriptor->label);
    leafNode.locationText = MemberLocationText(sectionName, memberName);
    leafNode.descriptionKey = std::string(descriptor->descriptionKey);
    leafNode.leaf.emplace(LayoutEditTreeLeaf{
        *key,
        sectionName,
        memberName,
        leafNode.descriptionKey,
        descriptor->valueFormat,
    });
    leafNode.selectionHighlight.emplace(leafNode.leaf->focusKey);
    return true;
}

bool BuildContainerNode(const std::string& sectionName,
    const std::string& memberName,
    const std::string& editCardId,
    const LayoutNodeConfig& node,
    const std::vector<size_t>& nodePath,
    LayoutEditTreeNode& treeNode) {
    // Size: out-param builders avoid optional<LayoutEditTreeNode> temporaries for large nodes.
    if (node.name != "rows" && node.name != "columns") {
        return false;
    }

    treeNode.kind = LayoutEditTreeNodeKind::Container;
    treeNode.label = node.name;
    treeNode.locationText = ContainerLocationText(sectionName, memberName, node.name);
    treeNode.descriptionKey = ContainerDescriptionKey(node.name);
    treeNode.initiallyExpanded = false;
    treeNode.selectionHighlight.emplace(LayoutContainerEditKey{editCardId, nodePath});

    std::vector<size_t> childPath = nodePath;
    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        childPath.push_back(i);
        LayoutEditTreeNode childNode;
        if (BuildContainerNode(sectionName, memberName, editCardId, child, childPath, childNode)) {
            treeNode.children.push_back(std::move(childNode));
        } else if (child.name == "metric_list") {
            LayoutEditTreeNode leafNode;
            if (BuildNodeFieldLeaf(sectionName, memberName, editCardId, childPath, WidgetClass::MetricList, leafNode)) {
                treeNode.children.push_back(std::move(leafNode));
            }
        } else if (child.name == "clock_time" || child.name == "clock_date") {
            const auto widgetClass = EnumFromString<WidgetClass>(child.name);
            if (widgetClass.has_value()) {
                LayoutEditTreeNode leafNode;
                if (BuildNodeFieldLeaf(sectionName, memberName, editCardId, childPath, *widgetClass, leafNode)) {
                    treeNode.children.push_back(std::move(leafNode));
                }
            }
        }
        childPath.pop_back();

        if (i + 1 < node.children.size() && SeparatorIsEditable(node, i)) {
            LayoutEditTreeNode leafNode;
            leafNode.kind = LayoutEditTreeNodeKind::Leaf;
            leafNode.label = FormatText(
                "%s, %s", ChildDisplayName(node.children[i]).c_str(), ChildDisplayName(node.children[i + 1]).c_str());
            leafNode.locationText = MemberLocationText(sectionName, memberName);
            leafNode.descriptionKey = "layout_edit.layout_guide";
            leafNode.leaf.emplace(LayoutEditTreeLeaf{
                LayoutWeightEditKey{editCardId, nodePath, i},
                sectionName,
                memberName,
                leafNode.descriptionKey,
                configschema::ValueFormat::Integer,
                node.name == "columns" ? LayoutGuideAxis::Vertical : LayoutGuideAxis::Horizontal,
                ChildDisplayName(node.children[i]),
                ChildDisplayName(node.children[i + 1]),
            });
            leafNode.selectionHighlight.emplace(leafNode.leaf->focusKey);
            treeNode.children.push_back(std::move(leafNode));
        }
    }

    if (treeNode.children.empty()) {
        return false;
    }
    if (treeNode.children.size() == 1) {
        LayoutEditTreeNode childNode = std::move(treeNode.children.front());
        MoveLayoutEditTreeNode(treeNode, std::move(childNode));
    }
    return true;
}

bool BuildStructureGroup(const std::string& sectionName,
    const std::string& memberName,
    const std::string& editCardId,
    const std::optional<LayoutEditSelectionHighlight>& selectionHighlight,
    const LayoutNodeConfig& node,
    LayoutEditTreeNode& groupNode) {
    groupNode.kind = LayoutEditTreeNodeKind::Group;
    groupNode.label = memberName;
    groupNode.locationText = MemberLocationText(sectionName, memberName);
    groupNode.descriptionKey = GroupDescriptionKey(memberName);
    groupNode.initiallyExpanded = false;
    if (selectionHighlight.has_value()) {
        groupNode.selectionHighlight.emplace(*selectionHighlight);
    }
    LayoutEditTreeNode containerNode;
    if (BuildContainerNode(sectionName, memberName, editCardId, node, {}, containerNode)) {
        if (containerNode.kind == LayoutEditTreeNodeKind::Container) {
            groupNode.children = std::move(containerNode.children);
        } else {
            groupNode.children.push_back(std::move(containerNode));
        }
    }
    if (groupNode.children.empty()) {
        return false;
    }
    return true;
}

bool BuildStaticSectionNode(const AppConfig& config, const TemplateSectionSlot& slot, LayoutEditTreeNode& sectionNode) {
    if (slot.sectionName == "metrics") {
        sectionNode.kind = LayoutEditTreeNodeKind::Section;
        sectionNode.label = slot.sectionName;
        sectionNode.locationText = SectionLocationText(slot.sectionName);
        sectionNode.descriptionKey = SectionDescriptionKey(slot.sectionName);
        sectionNode.initiallyExpanded = true;

        std::vector<std::string> orderedMetricIds;
        orderedMetricIds.reserve(slot.keys.size());
        for (const auto& key : slot.keys) {
            if (FindMetricDefinition(config.layout.metrics, key) != nullptr) {
                orderedMetricIds.push_back(key);
            }
        }
        for (const auto& definition : config.layout.metrics.definitions) {
            if (!ContainsString(orderedMetricIds, definition.id)) {
                orderedMetricIds.push_back(definition.id);
            }
        }

        for (const auto& metricId : orderedMetricIds) {
            LayoutEditTreeNode leafNode;
            leafNode.kind = LayoutEditTreeNodeKind::Leaf;
            leafNode.label = metricId;
            leafNode.locationText = MemberLocationText(slot.sectionName, metricId);
            leafNode.descriptionKey = "layout_edit.metric_definition";
            leafNode.leaf.emplace(LayoutEditTreeLeaf{
                LayoutMetricEditKey{metricId},
                slot.sectionName,
                metricId,
                leafNode.descriptionKey,
                configschema::ValueFormat::FloatingPoint,
            });
            leafNode.selectionHighlight.emplace(leafNode.leaf->focusKey);
            sectionNode.children.push_back(std::move(leafNode));
        }

        return !sectionNode.children.empty();
    }

    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = slot.sectionName;
    sectionNode.locationText = SectionLocationText(slot.sectionName);
    sectionNode.descriptionKey = SectionDescriptionKey(slot.sectionName);
    sectionNode.initiallyExpanded = true;
    if (const auto selectionHighlight = SectionSelectionHighlight(slot.sectionName); selectionHighlight.has_value()) {
        sectionNode.selectionHighlight.emplace(*selectionHighlight);
    }
    for (const auto& key : slot.keys) {
        const auto parameter = FindLayoutEditParameterByConfigField(slot.sectionName, key);
        if (!parameter.has_value()) {
            continue;
        }
        const auto descriptor = FindLayoutEditTooltipDescriptor(*parameter);
        if (!descriptor.has_value()) {
            continue;
        }
        LayoutEditTreeNode leafNode;
        leafNode.kind = LayoutEditTreeNodeKind::Leaf;
        leafNode.label = key;
        leafNode.locationText = MemberLocationText(descriptor->sectionName, descriptor->memberName);
        leafNode.descriptionKey = descriptor->configKey;
        leafNode.leaf.emplace(LayoutEditTreeLeaf{
            *parameter,
            descriptor->sectionName,
            descriptor->memberName,
            descriptor->configKey,
            descriptor->valueFormat,
        });
        leafNode.selectionHighlight.emplace(leafNode.leaf->focusKey);
        sectionNode.children.push_back(std::move(leafNode));
    }
    if (sectionNode.children.empty()) {
        return false;
    }
    return true;
}

bool BuildActiveLayoutSectionNode(const AppConfig& config, LayoutEditTreeNode& sectionNode) {
    if (config.display.layout.empty()) {
        return false;
    }
    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = FormatText("layout.%s", config.display.layout.c_str());
    sectionNode.locationText = SectionLocationText(sectionNode.label);
    sectionNode.descriptionKey = SectionDescriptionKey(sectionNode.label);
    sectionNode.initiallyExpanded = true;
    sectionNode.selectionHighlight.emplace(LayoutEditSelectionHighlightSpecial::DashboardBounds);
    LayoutEditTreeNode groupNode;
    if (BuildStructureGroup(sectionNode.label,
            "cards",
            "",
            sectionNode.selectionHighlight,
            config.layout.structure.cardsLayout,
            groupNode)) {
        sectionNode.children.push_back(std::move(groupNode));
    }
    return true;
}

const ThemeConfig* FindActiveTheme(const AppConfig& config) {
    for (const ThemeConfig& theme : config.layout.themes) {
        if (theme.name == config.display.theme) {
            return &theme;
        }
    }
    return nullptr;
}

bool BuildActiveThemeSectionNode(const AppConfig& config, LayoutEditTreeNode& sectionNode) {
    const ThemeConfig* theme = FindActiveTheme(config);
    if (theme == nullptr) {
        return false;
    }

    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = FormatText("theme.%s", theme->name.c_str());
    sectionNode.locationText = SectionLocationText(sectionNode.label);
    sectionNode.descriptionKey = SectionDescriptionKey(sectionNode.label);
    sectionNode.initiallyExpanded = true;

    for (const std::string& token : {"background", "foreground", "accent", "guide"}) {
        LayoutEditTreeNode leafNode;
        leafNode.kind = LayoutEditTreeNodeKind::Leaf;
        leafNode.label = token;
        leafNode.locationText = MemberLocationText(sectionNode.label, token);
        leafNode.descriptionKey = FormatText("config.theme.%s", token.c_str());
        leafNode.leaf.emplace(LayoutEditTreeLeaf{
            ThemeColorEditKey{theme->name, token},
            sectionNode.label,
            token,
            leafNode.descriptionKey,
            configschema::ValueFormat::ColorHex,
        });
        sectionNode.children.push_back(std::move(leafNode));
    }

    return true;
}

bool BuildCardSectionNode(const LayoutCardConfig& card, bool includeTitleLeaf, LayoutEditTreeNode& sectionNode) {
    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = FormatText("card.%s", card.id.c_str());
    sectionNode.locationText = SectionLocationText(sectionNode.label);
    sectionNode.descriptionKey = SectionDescriptionKey(sectionNode.label);
    sectionNode.initiallyExpanded = true;
    sectionNode.selectionHighlight.emplace(
        LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome});
    if (includeTitleLeaf) {
        LayoutEditTreeNode titleLeaf;
        titleLeaf.kind = LayoutEditTreeNodeKind::Leaf;
        titleLeaf.label = "title";
        titleLeaf.locationText = MemberLocationText(sectionNode.label, "title");
        titleLeaf.descriptionKey = CardMemberDescriptionKey("title");
        titleLeaf.leaf.emplace(LayoutEditTreeLeaf{
            LayoutCardTitleEditKey{card.id},
            sectionNode.label,
            "title",
            titleLeaf.descriptionKey,
            configschema::ValueFormat::String,
        });
        titleLeaf.selectionHighlight.emplace(titleLeaf.leaf->focusKey);
        sectionNode.children.push_back(std::move(titleLeaf));
    }
    LayoutEditTreeNode groupNode;
    if (BuildStructureGroup(
            sectionNode.label, "layout", card.id, sectionNode.selectionHighlight, card.layout, groupNode)) {
        sectionNode.children.push_back(std::move(groupNode));
    }
    return true;
}

const LayoutEditTreeLeaf* FindLayoutEditTreeLeafRecursive(
    const std::vector<LayoutEditTreeNode>& nodes, const LayoutEditFocusKey& focusKey) {
    for (const auto& node : nodes) {
        if (node.leaf.has_value() && MatchesLayoutEditFocusKey(node.leaf->focusKey, focusKey)) {
            return &(*node.leaf);
        }
        if (const auto* leaf = FindLayoutEditTreeLeafRecursive(node.children, focusKey); leaf != nullptr) {
            return leaf;
        }
    }
    return nullptr;
}

bool NodeMatchesFilter(const LayoutEditTreeNode& node, std::string_view loweredQuery) {
    if (loweredQuery.empty()) {
        return true;
    }
    const std::string loweredLabel = ToLower(node.label);
    if (loweredLabel.find(loweredQuery) != std::string::npos) {
        return true;
    }
    const std::string loweredLocation = ToLower(node.locationText);
    return loweredLocation.find(loweredQuery) != std::string::npos;
}

std::vector<LayoutEditTreeNode> FilterNodes(
    const std::vector<LayoutEditTreeNode>& nodes, std::string_view loweredQuery, bool forceExpand) {
    std::vector<LayoutEditTreeNode> filtered;
    for (const auto& node : nodes) {
        LayoutEditTreeNode candidate = node;
        candidate.children = FilterNodes(node.children, loweredQuery, true);
        const bool selfMatches = NodeMatchesFilter(node, loweredQuery);
        if (!selfMatches && candidate.children.empty()) {
            continue;
        }
        if (forceExpand && !candidate.children.empty()) {
            candidate.initiallyExpanded = true;
        }
        filtered.push_back(std::move(candidate));
    }
    return filtered;
}

}  // namespace

LayoutEditTreeModel BuildLayoutEditTreeModel(const AppConfig& config) {
    return BuildLayoutEditTreeModel(config, LoadEmbeddedConfigTemplate());
}

LayoutEditTreeModel BuildLayoutEditTreeModel(const AppConfig& config, std::string_view templateText) {
    LayoutEditTreeModel model;
    const std::vector<TemplateSectionSlot> sections = ParseTemplateSections(templateText);
    const std::vector<std::string> reachableCards = CollectReachableCards(config);
    const std::vector<std::string> topLevelCards = CollectTopLevelCards(config);

    for (const auto& section : sections) {
        switch (section.kind) {
            case TemplateSectionKind::StaticSection:
                if (LayoutEditTreeNode treeSection; BuildStaticSectionNode(config, section, treeSection)) {
                    model.roots.push_back(std::move(treeSection));
                }
                break;
            case TemplateSectionKind::ThemeSectionSlot:
                if (LayoutEditTreeNode themeSection; BuildActiveThemeSectionNode(config, themeSection)) {
                    model.roots.push_back(std::move(themeSection));
                }
                break;
            case TemplateSectionKind::LayoutSectionSlot:
                if (LayoutEditTreeNode layoutSection; BuildActiveLayoutSectionNode(config, layoutSection)) {
                    model.roots.push_back(std::move(layoutSection));
                }
                break;
            case TemplateSectionKind::CardSectionSlot:
                for (const auto& cardId : reachableCards) {
                    const LayoutCardConfig* card = FindCardConfig(config.layout, cardId);
                    if (card == nullptr) {
                        continue;
                    }
                    const bool includeTitleLeaf = ContainsString(topLevelCards, cardId);
                    if (LayoutEditTreeNode cardSection; BuildCardSectionNode(*card, includeTitleLeaf, cardSection)) {
                        model.roots.push_back(std::move(cardSection));
                    }
                }
                break;
        }
    }

    return model;
}

LayoutEditTreeModel FilterLayoutEditTreeModel(const LayoutEditTreeModel& model, std::string_view query) {
    const std::string loweredQuery = ToLower(Trim(query));
    if (loweredQuery.empty()) {
        return model;
    }

    LayoutEditTreeModel filtered;
    filtered.roots = FilterNodes(model.roots, loweredQuery, true);
    return filtered;
}

const LayoutEditTreeLeaf* FindLayoutEditTreeLeaf(const LayoutEditTreeModel& model, const LayoutEditFocusKey& focusKey) {
    return FindLayoutEditTreeLeafRecursive(model.roots, focusKey);
}
