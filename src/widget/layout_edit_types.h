#pragma once

#include <string>
#include <variant>
#include <vector>

#include "config/widget_class.h"
#include "widget/layout_edit_parameter_id.h"
#include "widget/render_types.h"

enum class LayoutGuideAxis {
    Horizontal,
    Vertical,
};

enum class AnchorShape {
    Circle,
    Diamond,
    Square,
    Wedge,
    VerticalReorder,
    Plus,
};

enum class AnchorDragAxis {
    Horizontal,
    Vertical,
    Both,
};

enum class AnchorDragMode {
    AxisDelta,
    RadialDistance,
};

struct LayoutEditWidgetIdentity {
    std::string renderCardId;
    std::string editCardId;
    std::vector<size_t> nodePath;

    enum class Kind {
        Widget,
        CardChrome,
        DashboardChrome,
    };

    Kind kind = Kind::Widget;
};

struct LayoutEditParameterSubject {
    LayoutEditWidgetIdentity widget;
    LayoutEditParameter parameter = LayoutEditParameter::MetricListBarHeight;
};

struct LayoutMetricEditKey {
    std::string metricId;
};

struct LayoutCardTitleEditKey {
    std::string cardId;
};

struct LayoutMetricListOrderEditKey {
    std::string editCardId;
    std::vector<size_t> nodePath;
};

struct LayoutEditLinearGeometry {
    RenderPoint drawStart{};
    RenderPoint drawEnd{};
    RenderRect hitRect{};
};

struct LayoutEditGuide {
    LayoutGuideAxis axis = LayoutGuideAxis::Horizontal;
    std::string renderCardId;
    std::string editCardId;
    std::vector<size_t> nodePath;
    size_t separatorIndex = 0;
    RenderRect containerRect{};
    RenderRect lineRect{};
    RenderRect hitRect{};
    int gap = 0;
    std::vector<int> childExtents;
    std::vector<bool> childFixedExtents;
    std::vector<RenderRect> childRects;
};

struct LayoutEditAnchorKey {
    LayoutEditWidgetIdentity widget;
    std::variant<LayoutEditParameter, LayoutMetricEditKey, LayoutCardTitleEditKey, LayoutMetricListOrderEditKey>
        subject = LayoutEditParameter::MetricListBarHeight;
    int anchorId = 0;
};

struct LayoutEditAnchorRegion {
    LayoutEditAnchorKey key;
    RenderRect targetRect{};
    RenderRect anchorRect{};
    RenderRect anchorHitRect{};
    int anchorHitPadding = 0;
    AnchorShape shape = AnchorShape::Circle;
    AnchorDragAxis dragAxis = AnchorDragAxis::Vertical;
    AnchorDragMode dragMode = AnchorDragMode::AxisDelta;
    RenderPoint dragOrigin{};
    double dragScale = 1.0;
    bool draggable = true;
    bool showWhenWidgetHovered = false;
    bool drawTargetOutline = true;
    int value = 0;
};

struct LayoutEditWidgetGuide : LayoutEditParameterSubject, LayoutEditLinearGeometry {
    LayoutGuideAxis axis = LayoutGuideAxis::Vertical;
    int guideId = 0;
    RenderRect widgetRect{};
    RenderPoint dragOrigin{};
    double value = 0.0;
    bool angularDrag = false;
    double angularMin = 0.0;
    double angularMax = 0.0;
    int dragDirection = 1;
};

struct LayoutEditGapAnchorKey : LayoutEditParameterSubject {
    std::vector<size_t> nodePath;
};

struct LayoutEditGapAnchor : LayoutEditLinearGeometry {
    LayoutGuideAxis axis = LayoutGuideAxis::Vertical;
    LayoutEditGapAnchorKey key;
    RenderRect handleRect{};
    AnchorDragAxis dragAxis = AnchorDragAxis::Vertical;
    double value = 0.0;
};

struct LayoutEditColorRegion {
    LayoutEditParameter parameter = LayoutEditParameter::ColorForeground;
    RenderRect targetRect{};
};

struct LayoutWeightEditKey {
    std::string editCardId;
    std::vector<size_t> nodePath;
    size_t separatorIndex = 0;
};

struct LayoutContainerEditKey {
    std::string editCardId;
    std::vector<size_t> nodePath;
};

enum class LayoutEditSelectionHighlightSpecial {
    AllCards,
    AllTexts,
    DashboardBounds,
};

struct LayoutGuideSnapCandidate {
    LayoutEditWidgetIdentity widget;
    int targetExtent = 0;
    int startExtent = 0;
    int startDistance = 0;
    size_t groupOrder = 0;
};

struct LayoutEditAnchorBinding {
    LayoutEditAnchorKey key;
    int value = 0;
    AnchorShape shape = AnchorShape::Circle;
    AnchorDragAxis dragAxis = AnchorDragAxis::Vertical;
    AnchorDragMode dragMode = AnchorDragMode::AxisDelta;
    bool draggable = true;
};

using TooltipPayload = std::
    variant<LayoutEditGuide, LayoutEditWidgetGuide, LayoutEditGapAnchor, LayoutEditAnchorRegion, LayoutEditColorRegion>;
using LayoutEditFocusKey = std::variant<LayoutEditParameter,
    LayoutWeightEditKey,
    LayoutMetricEditKey,
    LayoutCardTitleEditKey,
    LayoutMetricListOrderEditKey,
    LayoutContainerEditKey>;
using LayoutEditSelectionHighlight = std::variant<LayoutEditFocusKey,
    DashboardWidgetClass,
    LayoutContainerEditKey,
    LayoutEditWidgetIdentity,
    LayoutEditSelectionHighlightSpecial>;
