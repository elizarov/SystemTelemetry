#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "config/widget_class.h"
#include "renderer/render_types.h"
#include "widget/layout_edit_parameter_id.h"

struct WidgetLayout;

enum class LayoutGuideAxis : std::uint8_t {
    Horizontal,
    Vertical,
};

enum class AnchorShape : std::uint8_t {
    Circle,
    Diamond,
    Square,
    Wedge,
    VerticalReorder,
    HorizontalReorder,
    Plus,
};

enum class AnchorDragAxis : std::uint8_t {
    Horizontal,
    Vertical,
    Both,
};

enum class AnchorDragMode : std::uint8_t {
    AxisDelta,
    RadialDistance,
};

enum class LayoutEditAnchorVisibility : std::uint8_t {
    Always,
    WhenWidgetHovered,
};

enum class LayoutEditTargetOutline : std::uint8_t {
    Hidden,
    Visible,
};

struct LayoutEditAnchorDragSpec {
    AnchorDragAxis axis = AnchorDragAxis::Vertical;
    AnchorDragMode mode = AnchorDragMode::AxisDelta;
    double scale = 1.0;

    static LayoutEditAnchorDragSpec AxisDelta(AnchorDragAxis axis, double scale = 1.0);
    static LayoutEditAnchorDragSpec RadialDistance(double scale = 1.0);
};

struct LayoutEditAnchorDrag {
    AnchorDragAxis axis = AnchorDragAxis::Vertical;
    AnchorDragMode mode = AnchorDragMode::AxisDelta;
    RenderPoint origin{};
    double scale = 1.0;

    static LayoutEditAnchorDrag AxisDelta(AnchorDragAxis axis, RenderPoint origin, double scale = 1.0);
    static LayoutEditAnchorDrag RadialDistance(RenderPoint origin, double scale = 1.0);
};

struct LayoutEditWidgetIdentity {
    std::string renderCardId;
    std::string editCardId;
    std::vector<size_t> nodePath;

    enum class Kind : std::uint8_t {
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

struct ThemeColorEditKey {
    std::string themeName;
    std::string tokenName;
};

enum class LayoutNodeField : std::uint8_t {
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

struct LayoutEditOverlayOwner {
    LayoutContainerChildOrderEditKey key;
    int childIndex = 0;
};

enum class LayoutEditOverlayAffordanceLayer : std::uint8_t {
    Background,
    Foreground,
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
    std::vector<std::uint8_t> childFixedExtents;
    std::vector<RenderRect> childRects;
    std::vector<LayoutEditOverlayOwner> overlayOwners;
    LayoutEditOverlayAffordanceLayer overlayLayer = LayoutEditOverlayAffordanceLayer::Background;
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
    std::vector<LayoutEditOverlayOwner> overlayOwners;
    LayoutEditOverlayAffordanceLayer overlayLayer = LayoutEditOverlayAffordanceLayer::Background;
};

struct LayoutEditAnchorRegistration {
    LayoutEditAnchorKey key;
    RenderRect targetRect{};
    RenderRect anchorRect{};
    AnchorShape shape = AnchorShape::Circle;
    int value = 0;
    std::optional<LayoutEditAnchorDrag> drag = std::nullopt;
    LayoutEditAnchorVisibility visibility = LayoutEditAnchorVisibility::Always;
    LayoutEditTargetOutline targetOutline = LayoutEditTargetOutline::Visible;
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
    std::vector<LayoutEditOverlayOwner> overlayOwners;
    LayoutEditOverlayAffordanceLayer overlayLayer = LayoutEditOverlayAffordanceLayer::Background;
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
    std::vector<LayoutEditOverlayOwner> overlayOwners;
    LayoutEditOverlayAffordanceLayer overlayLayer = LayoutEditOverlayAffordanceLayer::Background;
};

struct LayoutEditColorRegion {
    LayoutEditParameter parameter = LayoutEditParameter::ColorForeground;
    RenderRect targetRect{};
    std::vector<LayoutEditOverlayOwner> overlayOwners;
    LayoutEditOverlayAffordanceLayer overlayLayer = LayoutEditOverlayAffordanceLayer::Background;
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

enum class LayoutEditSelectionHighlightSpecial : std::uint8_t {
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
    std::optional<LayoutEditAnchorDragSpec> drag =
        LayoutEditAnchorDragSpec{AnchorDragAxis::Vertical, AnchorDragMode::AxisDelta, 1.0};
};

using LayoutEditValue = std::variant<std::string, std::vector<std::string>>;

using TooltipPayload = std::
    variant<LayoutEditGuide, LayoutEditWidgetGuide, LayoutEditGapAnchor, LayoutEditAnchorRegion, LayoutEditColorRegion>;

using LayoutEditFocusKey = std::variant<LayoutEditParameter,
    LayoutWeightEditKey,
    LayoutMetricEditKey,
    LayoutCardTitleEditKey,
    ThemeColorEditKey,
    LayoutNodeFieldEditKey,
    LayoutContainerEditKey>;
using LayoutEditSelectionHighlight = std::variant<LayoutEditFocusKey,
    WidgetClass,
    LayoutContainerEditKey,
    LayoutEditWidgetIdentity,
    LayoutEditSelectionHighlightSpecial>;
