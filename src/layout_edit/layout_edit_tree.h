#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "config/config_schema.h"
#include "widget/layout_edit_types.h"

enum class LayoutEditTreeNodeKind {
    Section,
    Group,
    Container,
    Leaf,
};

struct LayoutEditTreeLeaf {
    LayoutEditFocusKey focusKey;
    std::string sectionName;
    std::string memberName;
    std::string descriptionKey;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::Integer;
    LayoutGuideAxis weightAxis = LayoutGuideAxis::Horizontal;
    std::string firstWeightName;
    std::string secondWeightName;
};

struct LayoutEditTreeNode {
    LayoutEditTreeNodeKind kind = LayoutEditTreeNodeKind::Section;
    std::string label;
    std::string locationText;
    std::string descriptionKey;
    bool initiallyExpanded = false;
    std::optional<LayoutEditSelectionHighlight> selectionHighlight;
    std::optional<LayoutEditTreeLeaf> leaf;
    std::vector<LayoutEditTreeNode> children;
};

struct LayoutEditTreeModel {
    std::vector<LayoutEditTreeNode> roots;
};

LayoutEditTreeModel BuildLayoutEditTreeModel(const AppConfig& config);
LayoutEditTreeModel BuildLayoutEditTreeModel(const AppConfig& config, std::string_view templateText);
LayoutEditTreeModel FilterLayoutEditTreeModel(const LayoutEditTreeModel& model, std::string_view query);
const LayoutEditTreeLeaf* FindLayoutEditTreeLeaf(const LayoutEditTreeModel& model, const LayoutEditFocusKey& focusKey);
