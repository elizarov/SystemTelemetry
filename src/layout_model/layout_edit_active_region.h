#pragma once

#include <string>
#include <variant>
#include <vector>

#include "config/widget_class.h"
#include "renderer/render_types.h"
#include "widget/layout_edit_types.h"

enum class LayoutEditActiveRegionKind {
    Card,
    CardHeader,
    WidgetHover,
    LayoutWeightGuide,
    ContainerChildReorderTarget,
    GapHandle,
    WidgetGuide,
    StaticEditAnchorHandle,
    StaticEditAnchorTarget,
    DynamicEditAnchorHandle,
    DynamicEditAnchorTarget,
    StaticColorTarget,
    DynamicColorTarget,
};

struct LayoutEditCardRegion {
    std::string id;
    std::vector<size_t> nodePath;
    RenderRect rect{};
    RenderRect headerRect{};
    bool hasHeader = false;
};

struct LayoutEditWidgetRegion {
    LayoutEditWidgetIdentity widget;
    WidgetClass widgetClass = WidgetClass::Unknown;
    RenderRect rect{};
    bool supportsSimilarityIndicator = false;
};

struct LayoutEditContainerChildReorderRegion {
    std::string renderCardId;
    std::string editCardId;
    std::vector<size_t> nodePath;
    bool horizontal = false;
    std::vector<RenderRect> childRects;
};

using LayoutEditActiveRegionPayload = std::variant<LayoutEditCardRegion,
    LayoutEditWidgetRegion,
    LayoutEditGuide,
    LayoutEditContainerChildReorderRegion,
    LayoutEditGapAnchor,
    LayoutEditWidgetGuide,
    LayoutEditAnchorRegion,
    LayoutEditColorRegion>;

struct LayoutEditActiveRegion {
    RenderRect box{};
    LayoutEditActiveRegionKind kind = LayoutEditActiveRegionKind::Card;
    LayoutEditActiveRegionPayload payload = LayoutEditCardRegion{};
};
