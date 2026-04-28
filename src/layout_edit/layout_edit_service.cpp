#include "layout_edit/layout_edit_service.h"

#include <algorithm>
#include <functional>

#include "layout_edit/layout_edit_target_descriptor.h"
#include "util/strings.h"

namespace {

constexpr std::string_view kMetricListPlaceholderId = "nothing";

LayoutCardConfig* FindCardLayoutById(LayoutConfig& layout, const std::string& cardId) {
    const auto it = std::find_if(
        layout.cards.begin(), layout.cards.end(), [&](LayoutCardConfig& card) { return card.id == cardId; });
    return it != layout.cards.end() ? &(*it) : nullptr;
}

LayoutSectionConfig* FindNamedLayoutByName(AppConfig& config, const std::string& name) {
    const auto it = std::find_if(config.layout.layouts.begin(),
        config.layout.layouts.end(),
        [&](LayoutSectionConfig& layout) { return layout.name == name; });
    return it != config.layout.layouts.end() ? &(*it) : nullptr;
}

LayoutNodeConfig* FindLayoutNodeByPath(LayoutNodeConfig& root, const std::vector<size_t>& path) {
    LayoutNodeConfig* node = &root;
    for (size_t index : path) {
        if (index >= node->children.size()) {
            return nullptr;
        }
        node = &node->children[index];
    }
    return node;
}

const LayoutNodeConfig* FindLayoutNodeByPath(const LayoutNodeConfig& root, const std::vector<size_t>& path) {
    const LayoutNodeConfig* node = &root;
    for (size_t index : path) {
        if (index >= node->children.size()) {
            return nullptr;
        }
        node = &node->children[index];
    }
    return node;
}

bool ApplyLayoutNodeMutation(AppConfig& config,
    const std::string& editCardId,
    const std::vector<size_t>& nodePath,
    const std::function<bool(LayoutNodeConfig*)>& mutate) {
    bool updated = false;
    if (editCardId.empty()) {
        updated = mutate(FindLayoutNodeByPath(config.layout.structure.cardsLayout, nodePath));
        if (!updated) {
            return false;
        }
        if (LayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            mutate(FindLayoutNodeByPath(namedLayout->cardsLayout, nodePath));
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, editCardId)) {
        updated = mutate(FindLayoutNodeByPath(card->layout, nodePath));
    }

    return updated;
}

std::string JoinMetricRefs(const std::vector<std::string>& metricRefs) {
    std::string parameter;
    for (size_t i = 0; i < metricRefs.size(); ++i) {
        if (i > 0) {
            parameter += ",";
        }
        parameter += metricRefs[i];
    }
    return parameter;
}

}  // namespace

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditLayoutTarget& target) {
    if (target.editCardId.empty()) {
        return FindLayoutNodeByPath(config.layout.structure.cardsLayout, target.nodePath);
    }
    const auto cardIt = std::find_if(config.layout.cards.begin(), config.layout.cards.end(), [&](const auto& card) {
        return card.id == target.editCardId;
    });
    if (cardIt == config.layout.cards.end()) {
        return nullptr;
    }
    return FindLayoutNodeByPath(cardIt->layout, target.nodePath);
}

const LayoutNodeConfig* FindEditableWidgetNode(const AppConfig& config, const LayoutEditWidgetIdentity& widget) {
    return FindGuideNode(config, LayoutEditLayoutTarget{widget.editCardId, widget.nodePath});
}

const LayoutNodeConfig* FindLayoutNodeFieldNode(const AppConfig& config, const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    if (descriptor == nullptr) {
        return nullptr;
    }
    const LayoutNodeConfig* node = FindGuideNode(config, LayoutEditLayoutTarget{key.editCardId, key.nodePath});
    const std::string_view expectedName = EnumToString(key.widgetClass);
    return node != nullptr && node->name == expectedName ? node : nullptr;
}

std::string ReadLayoutNodeFieldValue(const LayoutNodeConfig& node, LayoutNodeField field) {
    switch (field) {
        case LayoutNodeField::Parameter:
            return node.parameter;
    }
    return {};
}

std::vector<std::string> ParseMetricListMetricRefs(std::string_view parameter) {
    return SplitTrimmed(parameter, ',');
}

std::vector<std::string> AvailableMetricListMetricIds(const AppConfig& config, const ConfigMetricCatalog& catalog) {
    std::vector<std::string> metricIds;
    metricIds.reserve(config.layout.metrics.definitions.size());
    bool hasPlaceholder = false;
    for (const auto& definition : config.layout.metrics.definitions) {
        switch (definition.style) {
            case MetricDisplayStyle::Scalar:
            case MetricDisplayStyle::Percent:
            case MetricDisplayStyle::Memory:
                if (catalog.IsGenerallyAvailableMetric(definition.id)) {
                    hasPlaceholder = hasPlaceholder || definition.id == kMetricListPlaceholderId;
                    metricIds.push_back(definition.id);
                }
                break;
            case MetricDisplayStyle::Throughput:
            case MetricDisplayStyle::SizeAuto:
            case MetricDisplayStyle::LabelOnly:
                break;
        }
    }
    if (!hasPlaceholder && catalog.FindMetricDisplayStyle(kMetricListPlaceholderId).has_value()) {
        metricIds.insert(metricIds.begin(), std::string(kMetricListPlaceholderId));
    }
    return metricIds;
}

std::vector<int> SeedGuideWeights(const LayoutEditGuide& guide, const LayoutNodeConfig* node) {
    if (node == nullptr || node->children.size() != guide.childExtents.size()) {
        return guide.childExtents;
    }

    std::vector<int> weights;
    weights.reserve(node->children.size());
    for (size_t i = 0; i < node->children.size(); ++i) {
        weights.push_back(std::max(1, guide.childExtents[i]));
    }

    std::vector<bool> fixed = guide.childFixedExtents;
    if (fixed.size() != weights.size()) {
        fixed.assign(weights.size(), false);
    }

    for (size_t i = 0; i < weights.size(); ++i) {
        if (fixed[i]) {
            weights[i] = std::max(1, node->children[i].weight);
        }
    }

    return weights;
}

bool ApplyLayoutNodeFieldValue(AppConfig& config, const LayoutNodeFieldEditKey& key, std::string_view value) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    if (descriptor == nullptr || (descriptor->editorKind == LayoutEditEditorKind::DateTimeFormat && value.empty())) {
        return false;
    }

    const auto applyValue = [&](LayoutNodeConfig* node) -> bool {
        if (node == nullptr || node->name != EnumToString(key.widgetClass)) {
            return false;
        }
        switch (key.field) {
            case LayoutNodeField::Parameter:
                node->parameter = std::string(value);
                return true;
        }
        return false;
    };
    return ApplyLayoutNodeMutation(config, key.editCardId, key.nodePath, applyValue);
}

bool ApplyLayoutEditValue(AppConfig& config, const LayoutEditFocusKey& key, const LayoutEditValue& value) {
    const auto* nodeFieldKey = std::get_if<LayoutNodeFieldEditKey>(&key);
    if (nodeFieldKey == nullptr) {
        return false;
    }
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(*nodeFieldKey);
    if (descriptor == nullptr) {
        return false;
    }
    if (descriptor->editorKind == LayoutEditEditorKind::MetricListOrder) {
        const auto* metricRefs = std::get_if<std::vector<std::string>>(&value);
        return metricRefs != nullptr && ApplyLayoutNodeFieldValue(config, *nodeFieldKey, JoinMetricRefs(*metricRefs));
    }
    const auto* text = std::get_if<std::string>(&value);
    return text != nullptr && ApplyLayoutNodeFieldValue(config, *nodeFieldKey, *text);
}

bool ApplyMetricListOrder(
    AppConfig& config, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) {
    const LayoutNodeFieldEditKey key{
        widget.editCardId, widget.nodePath, WidgetClass::MetricList, LayoutNodeField::Parameter};
    return ApplyLayoutEditValue(config, LayoutEditFocusKey{key}, LayoutEditValue{metricRefs});
}

bool ApplyContainerChildOrder(
    AppConfig& config, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex) {
    const auto applyOrder = [&](LayoutNodeConfig* node) -> bool {
        if (node == nullptr || (node->name != "rows" && node->name != "columns") || fromIndex < 0 || toIndex < 0 ||
            fromIndex >= static_cast<int>(node->children.size()) ||
            toIndex >= static_cast<int>(node->children.size())) {
            return false;
        }
        if (fromIndex == toIndex) {
            return true;
        }
        LayoutNodeConfig moved = std::move(node->children[static_cast<size_t>(fromIndex)]);
        node->children.erase(node->children.begin() + fromIndex);
        node->children.insert(node->children.begin() + toIndex, std::move(moved));
        return true;
    };

    bool updated = false;
    if (key.editCardId.empty()) {
        updated = applyOrder(FindLayoutNodeByPath(config.layout.structure.cardsLayout, key.nodePath));
        if (!updated) {
            return false;
        }
        if (LayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            applyOrder(FindLayoutNodeByPath(namedLayout->cardsLayout, key.nodePath));
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, key.editCardId)) {
        updated = applyOrder(FindLayoutNodeByPath(card->layout, key.nodePath));
    }

    return updated;
}

bool AppendMetricListRow(AppConfig& config, const LayoutEditWidgetIdentity& widget, std::string_view metricRef) {
    if (metricRef.empty()) {
        return false;
    }

    const LayoutNodeConfig* currentNode =
        FindGuideNode(config, LayoutEditLayoutTarget{widget.editCardId, widget.nodePath});
    if (currentNode == nullptr || currentNode->name != "metric_list") {
        return false;
    }

    std::vector<std::string> metricRefs = ParseMetricListMetricRefs(currentNode->parameter);
    metricRefs.push_back(std::string(metricRef));
    return ApplyMetricListOrder(config, widget, metricRefs);
}
