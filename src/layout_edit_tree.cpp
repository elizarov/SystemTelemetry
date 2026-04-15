#include "layout_edit_tree.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>

#include "config_parser.h"
#include "layout_edit_parameter.h"

namespace {

enum class TemplateSectionKind {
    StaticSection,
    LayoutSectionSlot,
    CardSectionSlot,
};

struct TemplateSectionSlot {
    TemplateSectionKind kind = TemplateSectionKind::StaticSection;
    std::string sectionName;
    std::vector<std::string> keys;
};

std::string Trim(std::string_view value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::vector<TemplateSectionSlot> ParseTemplateSections(std::string_view text) {
    std::vector<TemplateSectionSlot> sections;
    TemplateSectionSlot* currentStaticSection = nullptr;
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
                if (sectionName.rfind("layout.", 0) == 0) {
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
    const auto widgetClass = FindDashboardWidgetClass(sectionName);
    if (!widgetClass.has_value()) {
        return std::nullopt;
    }
    return LayoutEditSelectionHighlight{*widgetClass};
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
    const auto it = std::find_if(
        layout.cards.begin(), layout.cards.end(), [&](const LayoutCardConfig& card) { return card.id == cardId; });
    return it != layout.cards.end() ? &(*it) : nullptr;
}

void CollectReachableCardLayoutCards(const LayoutConfig& layout,
    const LayoutNodeConfig& node,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack,
    std::unordered_map<std::string, bool>& seenCards);

void CollectReachableCardById(const LayoutConfig& layout,
    const std::string& cardId,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack,
    std::unordered_map<std::string, bool>& seenCards) {
    if (std::find(recursionStack.begin(), recursionStack.end(), cardId) != recursionStack.end()) {
        return;
    }
    if (!seenCards.contains(cardId)) {
        orderedCards.push_back(cardId);
        seenCards.emplace(cardId, true);
    }

    const LayoutCardConfig* card = FindCardConfig(layout, cardId);
    if (card == nullptr) {
        return;
    }

    recursionStack.push_back(cardId);
    CollectReachableCardLayoutCards(layout, card->layout, orderedCards, recursionStack, seenCards);
    recursionStack.pop_back();
}

void CollectReachableDashboardCards(const LayoutConfig& layout,
    const LayoutNodeConfig& node,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack,
    std::unordered_map<std::string, bool>& seenCards) {
    if (node.name == "rows" || node.name == "columns") {
        for (const auto& child : node.children) {
            CollectReachableDashboardCards(layout, child, orderedCards, recursionStack, seenCards);
        }
        return;
    }

    if (!node.name.empty()) {
        CollectReachableCardById(layout, node.name, orderedCards, recursionStack, seenCards);
    }
}

void CollectReachableCardLayoutCards(const LayoutConfig& layout,
    const LayoutNodeConfig& node,
    std::vector<std::string>& orderedCards,
    std::vector<std::string>& recursionStack,
    std::unordered_map<std::string, bool>& seenCards) {
    if (node.cardReference) {
        CollectReachableCardById(layout, node.name, orderedCards, recursionStack, seenCards);
        return;
    }

    for (const auto& child : node.children) {
        CollectReachableCardLayoutCards(layout, child, orderedCards, recursionStack, seenCards);
    }
}

std::vector<std::string> CollectReachableCards(const AppConfig& config) {
    std::vector<std::string> orderedCards;
    std::vector<std::string> recursionStack;
    std::unordered_map<std::string, bool> seenCards;
    CollectReachableDashboardCards(
        config.layout, config.layout.structure.cardsLayout, orderedCards, recursionStack, seenCards);
    return orderedCards;
}

std::optional<LayoutEditTreeNode> BuildContainerNode(const std::string& sectionName,
    const std::string& memberName,
    const std::string& editCardId,
    const LayoutNodeConfig& node,
    const std::vector<size_t>& nodePath) {
    if (node.name != "rows" && node.name != "columns") {
        return std::nullopt;
    }

    LayoutEditTreeNode treeNode;
    treeNode.kind = LayoutEditTreeNodeKind::Container;
    treeNode.label = node.name;
    treeNode.initiallyExpanded = false;
    treeNode.selectionHighlight = LayoutContainerEditKey{editCardId, nodePath};

    for (size_t i = 0; i < node.children.size(); ++i) {
        const auto& child = node.children[i];
        std::vector<size_t> childPath = nodePath;
        childPath.push_back(i);
        if (const auto childContainer = BuildContainerNode(sectionName, memberName, editCardId, child, childPath);
            childContainer.has_value()) {
            treeNode.children.push_back(*childContainer);
        }

        if (i + 1 < node.children.size() && SeparatorIsEditable(node, i)) {
            LayoutEditTreeNode leafNode;
            leafNode.kind = LayoutEditTreeNodeKind::Leaf;
            leafNode.label = ChildDisplayName(node.children[i]) + ", " + ChildDisplayName(node.children[i + 1]);
            leafNode.leaf = LayoutEditTreeLeaf{
                LayoutWeightEditKey{editCardId, nodePath, i},
                sectionName,
                memberName,
                "layout_edit.layout_guide",
                configschema::ValueFormat::Integer,
                node.name == "columns" ? LayoutGuideAxis::Vertical : LayoutGuideAxis::Horizontal,
                ChildDisplayName(node.children[i]),
                ChildDisplayName(node.children[i + 1]),
            };
            leafNode.selectionHighlight = leafNode.leaf->focusKey;
            treeNode.children.push_back(std::move(leafNode));
        }
    }

    if (treeNode.children.empty()) {
        return std::nullopt;
    }
    if (treeNode.children.size() == 1) {
        return treeNode.children.front();
    }
    return treeNode;
}

std::optional<LayoutEditTreeNode> BuildStructureGroup(const std::string& sectionName,
    const std::string& memberName,
    const std::string& editCardId,
    const LayoutNodeConfig& node) {
    LayoutEditTreeNode groupNode;
    groupNode.kind = LayoutEditTreeNodeKind::Group;
    groupNode.label = memberName;
    groupNode.initiallyExpanded = false;
    if (const auto containerNode = BuildContainerNode(sectionName, memberName, editCardId, node, {});
        containerNode.has_value()) {
        if (containerNode->kind == LayoutEditTreeNodeKind::Container) {
            groupNode.children = containerNode->children;
        } else {
            groupNode.children.push_back(*containerNode);
        }
    }
    if (groupNode.children.empty()) {
        return std::nullopt;
    }
    return groupNode;
}

std::optional<LayoutEditTreeNode> BuildStaticSectionNode(const TemplateSectionSlot& slot) {
    LayoutEditTreeNode sectionNode;
    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = slot.sectionName;
    sectionNode.initiallyExpanded = true;
    if (const auto selectionHighlight = SectionSelectionHighlight(slot.sectionName); selectionHighlight.has_value()) {
        sectionNode.selectionHighlight = *selectionHighlight;
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
        leafNode.leaf = LayoutEditTreeLeaf{
            *parameter,
            descriptor->sectionName,
            descriptor->memberName,
            descriptor->configKey,
            descriptor->valueFormat,
        };
        leafNode.selectionHighlight = leafNode.leaf->focusKey;
        sectionNode.children.push_back(std::move(leafNode));
    }
    if (sectionNode.children.empty()) {
        return std::nullopt;
    }
    return sectionNode;
}

std::optional<LayoutEditTreeNode> BuildActiveLayoutSectionNode(const AppConfig& config) {
    if (config.display.layout.empty()) {
        return std::nullopt;
    }
    LayoutEditTreeNode sectionNode;
    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = "layout." + config.display.layout;
    sectionNode.initiallyExpanded = true;
    sectionNode.selectionHighlight = LayoutEditSelectionHighlight{LayoutEditSelectionHighlightSpecial::DashboardBounds};
    if (const auto groupNode = BuildStructureGroup(sectionNode.label, "cards", "", config.layout.structure.cardsLayout);
        groupNode.has_value()) {
        sectionNode.children.push_back(*groupNode);
    }
    if (sectionNode.children.empty()) {
        return std::nullopt;
    }
    return sectionNode;
}

std::optional<LayoutEditTreeNode> BuildCardSectionNode(const LayoutCardConfig& card) {
    LayoutEditTreeNode sectionNode;
    sectionNode.kind = LayoutEditTreeNodeKind::Section;
    sectionNode.label = "card." + card.id;
    sectionNode.initiallyExpanded = true;
    sectionNode.selectionHighlight = LayoutEditSelectionHighlight{
        LayoutEditWidgetIdentity{card.id, card.id, {}, LayoutEditWidgetIdentity::Kind::CardChrome}};
    if (const auto groupNode = BuildStructureGroup(sectionNode.label, "layout", card.id, card.layout);
        groupNode.has_value()) {
        sectionNode.children.push_back(*groupNode);
    }
    if (sectionNode.children.empty()) {
        return std::nullopt;
    }
    return sectionNode;
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

}  // namespace

LayoutEditTreeModel BuildLayoutEditTreeModel(const AppConfig& config) {
    return BuildLayoutEditTreeModel(config, LoadEmbeddedConfigTemplate());
}

LayoutEditTreeModel BuildLayoutEditTreeModel(const AppConfig& config, std::string_view templateText) {
    LayoutEditTreeModel model;
    const std::vector<TemplateSectionSlot> sections = ParseTemplateSections(templateText);
    const std::vector<std::string> reachableCards = CollectReachableCards(config);

    for (const auto& section : sections) {
        switch (section.kind) {
            case TemplateSectionKind::StaticSection:
                if (const auto treeSection = BuildStaticSectionNode(section); treeSection.has_value()) {
                    model.roots.push_back(*treeSection);
                }
                break;
            case TemplateSectionKind::LayoutSectionSlot:
                if (const auto layoutSection = BuildActiveLayoutSectionNode(config); layoutSection.has_value()) {
                    model.roots.push_back(*layoutSection);
                }
                break;
            case TemplateSectionKind::CardSectionSlot:
                for (const auto& cardId : reachableCards) {
                    const LayoutCardConfig* card = FindCardConfig(config.layout, cardId);
                    if (card == nullptr) {
                        continue;
                    }
                    if (const auto cardSection = BuildCardSectionNode(*card); cardSection.has_value()) {
                        model.roots.push_back(*cardSection);
                    }
                }
                break;
        }
    }

    return model;
}

const LayoutEditTreeLeaf* FindLayoutEditTreeLeaf(const LayoutEditTreeModel& model, const LayoutEditFocusKey& focusKey) {
    return FindLayoutEditTreeLeafRecursive(model.roots, focusKey);
}
