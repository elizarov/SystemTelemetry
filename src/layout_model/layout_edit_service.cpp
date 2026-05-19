#include "layout_model/layout_edit_service.h"

#include <algorithm>

#include "config/config_def.h"

namespace {

LayoutCardConfig* FindCardLayoutById(LayoutConfig& layout, const std::string& cardId) {
    for (LayoutCardConfig& card : layout.cards) {
        if (card.id == cardId) {
            return &card;
        }
    }
    return nullptr;
}

LayoutSectionConfig* FindNamedLayoutByName(AppConfig& config, const std::string& name) {
    for (LayoutSectionConfig& layout : config.layout.layouts) {
        if (layout.name == name) {
            return &layout;
        }
    }
    return nullptr;
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

bool ApplyWeightsToNode(LayoutNodeConfig* node, const std::vector<int>& weights) {
    if (node == nullptr || node->children.size() != weights.size()) {
        return false;
    }
    for (size_t i = 0; i < weights.size(); ++i) {
        node->children[i].weight = std::max(1, weights[i]);
    }
    return true;
}

bool ApplyAdjacentWeightsToNode(LayoutNodeConfig* node, size_t separatorIndex, int firstWeight, int secondWeight) {
    if (node == nullptr || separatorIndex + 1 >= node->children.size()) {
        return false;
    }
    for (LayoutNodeConfig& child : node->children) {
        child.weight = std::max(1, child.weight);
    }
    node->children[separatorIndex].weight = std::max(1, firstWeight);
    node->children[separatorIndex + 1].weight = std::max(1, secondWeight);
    return true;
}

}  // namespace

bool ApplyGuideWeights(AppConfig& config, const LayoutEditLayoutTarget& target, const std::vector<int>& weights) {
    if (weights.size() < 2) {
        return false;
    }

    bool updated = false;
    if (target.editCardId.empty()) {
        updated = ApplyWeightsToNode(FindLayoutNodeByPath(config.layout.structure.cards, target.nodePath), weights);
        if (!updated) {
            return false;
        }
        if (LayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            ApplyWeightsToNode(FindLayoutNodeByPath(namedLayout->cards, target.nodePath), weights);
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, target.editCardId)) {
        updated = ApplyWeightsToNode(FindLayoutNodeByPath(card->layout, target.nodePath), weights);
    }

    return updated;
}

bool ApplyGuideAdjacentWeights(
    AppConfig& config, const LayoutEditLayoutTarget& target, size_t separatorIndex, int firstWeight, int secondWeight) {
    bool updated = false;
    if (target.editCardId.empty()) {
        updated = ApplyAdjacentWeightsToNode(FindLayoutNodeByPath(config.layout.structure.cards, target.nodePath),
            separatorIndex,
            firstWeight,
            secondWeight);
        if (!updated) {
            return false;
        }
        if (LayoutSectionConfig* namedLayout = FindNamedLayoutByName(config, config.display.layout)) {
            ApplyAdjacentWeightsToNode(
                FindLayoutNodeByPath(namedLayout->cards, target.nodePath), separatorIndex, firstWeight, secondWeight);
        }
    } else if (LayoutCardConfig* card = FindCardLayoutById(config.layout, target.editCardId)) {
        updated = ApplyAdjacentWeightsToNode(
            FindLayoutNodeByPath(card->layout, target.nodePath), separatorIndex, firstWeight, secondWeight);
    }

    return updated;
}
