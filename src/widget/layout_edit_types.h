#pragma once

#include <optional>
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

inline bool operator==(const LayoutMetricEditKey& left, const LayoutMetricEditKey& right) {
    return left.metricId == right.metricId;
}

struct LayoutCardTitleEditKey {
    std::string cardId;
};

inline bool operator==(const LayoutCardTitleEditKey& left, const LayoutCardTitleEditKey& right) {
    return left.cardId == right.cardId;
}

struct LayoutMetricListOrderEditKey {
    std::string editCardId;
    std::vector<size_t> nodePath;
};

inline bool operator==(const LayoutMetricListOrderEditKey& left, const LayoutMetricListOrderEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath;
}

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

bool MatchesWidgetIdentity(const LayoutEditWidgetIdentity& left, const LayoutEditWidgetIdentity& right);
bool MatchesParameterSubject(const LayoutEditParameterSubject& left, const LayoutEditParameterSubject& right);
bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right);
bool MatchesGapEditAnchorKey(const LayoutEditGapAnchorKey& left, const LayoutEditGapAnchorKey& right);
bool MatchesEditableAnchorKey(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right);
bool MatchesWidgetEditGuide(const LayoutEditWidgetGuide& left, const LayoutEditWidgetGuide& right);
bool MatchesLayoutContainerEditKey(const LayoutContainerEditKey& left, const LayoutContainerEditKey& right);
bool MatchesLayoutWeightEditKey(const LayoutWeightEditKey& left, const LayoutWeightEditKey& right);
bool MatchesLayoutMetricEditKey(const LayoutMetricEditKey& left, const LayoutMetricEditKey& right);
bool MatchesLayoutCardTitleEditKey(const LayoutCardTitleEditKey& left, const LayoutCardTitleEditKey& right);
bool MatchesLayoutMetricListOrderEditKey(
    const LayoutMetricListOrderEditKey& left, const LayoutMetricListOrderEditKey& right);
bool MatchesCardChromeSelectionIdentity(
    const LayoutEditWidgetIdentity& selection, const LayoutEditWidgetIdentity& candidate);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& left, const LayoutEditFocusKey& right);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGuide& guide);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditWidgetGuide& guide);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGapAnchorKey& key);
bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditAnchorKey& key);
bool MatchesLayoutEditSelectionHighlight(const LayoutEditSelectionHighlight& highlight, const LayoutEditGuide& guide);
bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditWidgetGuide& guide);
bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditGapAnchorKey& key);
bool MatchesLayoutEditSelectionHighlight(const LayoutEditSelectionHighlight& highlight, const LayoutEditAnchorKey& key);
bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditColorRegion& region);
std::optional<LayoutEditParameter> LayoutEditAnchorParameter(const LayoutEditAnchorKey& key);
std::optional<LayoutMetricEditKey> LayoutEditAnchorMetricKey(const LayoutEditAnchorKey& key);
std::optional<LayoutCardTitleEditKey> LayoutEditAnchorCardTitleKey(const LayoutEditAnchorKey& key);
std::optional<LayoutMetricListOrderEditKey> LayoutEditAnchorMetricListOrderKey(const LayoutEditAnchorKey& key);
bool IsLayoutGuidePayload(const TooltipPayload& payload);
std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload);
std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload);
std::optional<unsigned int> TooltipPayloadColorValue(const TooltipPayload& payload);
RenderPoint TooltipPayloadAnchorPoint(const TooltipPayload& payload);
std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload);
