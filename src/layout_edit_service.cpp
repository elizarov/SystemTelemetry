#include "layout_edit_service.h"

#include <algorithm>

namespace {

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
