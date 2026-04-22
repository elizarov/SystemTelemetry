#include "layout_edit/layout_edit_service.h"

#include <algorithm>
#include <cctype>

#include "dashboard/dashboard_metrics.h"

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

}  // namespace

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditHost::LayoutTarget& target) {
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
    LayoutEditHost::LayoutTarget target;
    target.editCardId = widget.editCardId;
    target.nodePath = widget.nodePath;
    return FindGuideNode(config, target);
}

const LayoutNodeConfig* FindMetricListNode(const AppConfig& config, const LayoutMetricListOrderEditKey& key) {
    LayoutEditHost::LayoutTarget target;
    target.editCardId = key.editCardId;
    target.nodePath = key.nodePath;
    const LayoutNodeConfig* node = FindGuideNode(config, target);
    return node != nullptr && node->name == "metric_list" ? node : nullptr;
}

std::vector<std::string> ParseMetricListMetricRefs(std::string_view parameter) {
    auto trim = [](std::string_view value) {
        size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
            ++first;
        }
        size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
            --last;
        }
        return value.substr(first, last - first);
    };

    std::vector<std::string> metricRefs;
    size_t start = 0;
    while (start <= parameter.size()) {
        const size_t comma = parameter.find(',', start);
        const std::string_view item =
            comma == std::string_view::npos ? parameter.substr(start) : parameter.substr(start, comma - start);
        const std::string_view trimmed = trim(item);
        if (!trimmed.empty()) {
            metricRefs.emplace_back(trimmed);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return metricRefs;
}

std::vector<std::string> AvailableMetricListMetricIds(const AppConfig& config) {
    std::vector<std::string> metricIds;
    metricIds.reserve(config.layout.metrics.definitions.size());
    bool hasPlaceholder = false;
    for (const auto& definition : config.layout.metrics.definitions) {
        switch (definition.style) {
            case MetricDisplayStyle::Scalar:
            case MetricDisplayStyle::Percent:
            case MetricDisplayStyle::Memory:
                if (IsGenerallyAvailableDashboardMetric(definition.id)) {
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
    if (!hasPlaceholder && FindDashboardMetricDisplayStyle(kMetricListPlaceholderId).has_value()) {
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

bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights) {
    if (weights.size() < 2) {
        return false;
    }

    const auto applyWeights = [&](LayoutNodeConfig* node) -> bool {
        if (node == nullptr || node->children.size() != weights.size()) {
            return false;
        }
        for (size_t i = 0; i < weights.size(); ++i) {
            node->children[i].weight = std::max(1, weights[i]);
        }
        return true;
    };

    bool updated = false;
    if (target.editCardId.empty()) {
        updated = applyWeights(FindLayoutNodeByPath(config.layout.structure.cardsLayout, target.nodePath));
        if (!updated) {
            return false;
        }
        if (LayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            applyWeights(FindLayoutNodeByPath(namedLayout->cardsLayout, target.nodePath));
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, target.editCardId)) {
        updated = applyWeights(FindLayoutNodeByPath(card->layout, target.nodePath));
    }

    return updated;
}

bool ApplyMetricListOrder(
    AppConfig& config, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs) {
    const auto applyOrder = [&](LayoutNodeConfig* node) -> bool {
        if (node == nullptr || node->name != "metric_list") {
            return false;
        }
        std::string parameter;
        for (size_t i = 0; i < metricRefs.size(); ++i) {
            if (i > 0) {
                parameter += ",";
            }
            parameter += metricRefs[i];
        }
        node->parameter = parameter;
        return true;
    };

    bool updated = false;
    if (widget.editCardId.empty()) {
        updated = applyOrder(FindLayoutNodeByPath(config.layout.structure.cardsLayout, widget.nodePath));
        if (!updated) {
            return false;
        }
        if (LayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            applyOrder(FindLayoutNodeByPath(namedLayout->cardsLayout, widget.nodePath));
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, widget.editCardId)) {
        updated = applyOrder(FindLayoutNodeByPath(card->layout, widget.nodePath));
    }

    return updated;
}

bool AppendMetricListRow(AppConfig& config, const LayoutEditWidgetIdentity& widget, std::string_view metricRef) {
    if (metricRef.empty()) {
        return false;
    }

    const LayoutEditHost::LayoutTarget target{widget.editCardId, widget.nodePath};
    const LayoutNodeConfig* currentNode = FindGuideNode(config, target);
    if (currentNode == nullptr || currentNode->name != "metric_list") {
        return false;
    }

    std::vector<std::string> metricRefs = ParseMetricListMetricRefs(currentNode->parameter);
    metricRefs.push_back(std::string(metricRef));
    return ApplyMetricListOrder(config, widget, metricRefs);
}

std::optional<int> EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const LayoutEditWidgetIdentity& widget,
    LayoutGuideAxis axis) {
    if (!renderer.ApplyLayoutGuideWeightsPreview(target.editCardId, target.nodePath, weights)) {
        return std::nullopt;
    }
    return renderer.FindLayoutWidgetExtent(widget, axis);
}
