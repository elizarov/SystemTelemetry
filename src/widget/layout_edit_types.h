#pragma once

#include <string>
#include <variant>
#include <vector>

#include "config/widget_class.h"
#include "renderer/render_types.h"
#include "widget/layout_edit_parameter_id.h"

struct WidgetLayout;

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
    HorizontalReorder,
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

enum class LayoutNodeField {
    Parameter,
};

struct LayoutNodeFieldEditKey {
    std::string editCardId;
    std::vector<size_t> nodePath;
    WidgetClass widgetClass = WidgetClass::Unknown;
    LayoutNodeField field = LayoutNodeField::Parameter;
};

LayoutNodeFieldEditKey MakeLayoutNodeFieldEditKey(const LayoutEditWidgetIdentity& widget,
    WidgetClass widgetClass,
    LayoutNodeField field = LayoutNodeField::Parameter);

struct LayoutContainerChildOrderEditKey {
    std::string editCardId;
    std::vector<size_t> nodePath;
};

struct MetricListReorderOverlayState {
    LayoutEditWidgetIdentity widget;
    int currentIndex = 0;
    int mouseY = 0;
    int dragOffsetY = 0;
};

struct ContainerChildReorderOverlayState {
    LayoutContainerChildOrderEditKey key;
    std::vector<RenderRect> childRects;
    int currentIndex = 0;
    int mouseCoordinate = 0;
    int dragOffset = 0;
    bool horizontal = false;
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
    std::variant<LayoutEditParameter,
        LayoutMetricEditKey,
        LayoutCardTitleEditKey,
        LayoutNodeFieldEditKey,
        LayoutContainerChildOrderEditKey>
        subject = LayoutEditParameter::MetricListBarHeight;
    int anchorId = 0;
};

LayoutEditAnchorKey MakeLayoutNodeFieldEditAnchorKey(const WidgetLayout& widget,
    WidgetClass widgetClass,
    int anchorId = 0,
    LayoutNodeField field = LayoutNodeField::Parameter);

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

using LayoutEditValue = std::variant<std::string, std::vector<std::string>>;

using TooltipPayload = std::
    variant<LayoutEditGuide, LayoutEditWidgetGuide, LayoutEditGapAnchor, LayoutEditAnchorRegion, LayoutEditColorRegion>;
using LayoutEditFocusKey = std::variant<LayoutEditParameter,
    LayoutWeightEditKey,
    LayoutMetricEditKey,
    LayoutCardTitleEditKey,
    LayoutNodeFieldEditKey,
    LayoutContainerEditKey>;
using LayoutEditSelectionHighlight = std::variant<LayoutEditFocusKey,
    WidgetClass,
    LayoutContainerEditKey,
    LayoutEditWidgetIdentity,
    LayoutEditSelectionHighlightSpecial>;
