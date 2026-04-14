#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "layout_edit_parameter.h"
#include "render_types.h"

enum class LayoutGuideAxis {
    Horizontal,
    Vertical,
};

enum class AnchorShape {
    Circle,
    Diamond,
    Square,
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

struct LayoutEditAnchorKey : LayoutEditParameterSubject {
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
};

using TooltipPayload = std::variant<LayoutEditGuide, LayoutEditWidgetGuide, LayoutEditGapAnchor, LayoutEditAnchorRegion>;

inline bool MatchesWidgetIdentity(const LayoutEditWidgetIdentity& left, const LayoutEditWidgetIdentity& right) {
    return left.kind == right.kind && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath;
}

inline bool MatchesParameterSubject(const LayoutEditParameterSubject& left, const LayoutEditParameterSubject& right) {
    return left.parameter == right.parameter && MatchesWidgetIdentity(left.widget, right.widget);
}

inline bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) {
    return left.axis == right.axis && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath && left.separatorIndex == right.separatorIndex;
}

inline bool MatchesGapEditAnchorKey(const LayoutEditGapAnchorKey& left, const LayoutEditGapAnchorKey& right) {
    return MatchesParameterSubject(left, right) && left.nodePath == right.nodePath;
}

inline bool MatchesEditableAnchorKey(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right) {
    return left.anchorId == right.anchorId && MatchesParameterSubject(left, right);
}

inline bool MatchesWidgetEditGuide(const LayoutEditWidgetGuide& left, const LayoutEditWidgetGuide& right) {
    return left.axis == right.axis && left.guideId == right.guideId && MatchesParameterSubject(left, right);
}

inline bool IsLayoutGuidePayload(const TooltipPayload& payload) {
    return std::holds_alternative<LayoutEditGuide>(payload);
}

inline std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<LayoutEditParameter> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide>) {
                return value.parameter;
            } else {
                return value.key.parameter;
            }
        },
        payload);
}

inline std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<double> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, LayoutEditAnchorRegion>) {
                return static_cast<double>(value.value);
            } else {
                return value.value;
            }
        },
        payload);
}

inline RenderPoint TooltipPayloadAnchorPoint(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> RenderPoint {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return RenderPoint{value.lineRect.left + (std::max<LONG>(0, value.lineRect.right - value.lineRect.left) / 2),
                    value.lineRect.top + (std::max<LONG>(0, value.lineRect.bottom - value.lineRect.top) / 2)};
            } else if constexpr (std::is_same_v<T, LayoutEditGapAnchor>) {
                return RenderPoint{
                    value.handleRect.left + (std::max<LONG>(0, value.handleRect.right - value.handleRect.left) / 2),
                    value.handleRect.top + (std::max<LONG>(0, value.handleRect.bottom - value.handleRect.top) / 2)};
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide>) {
                return value.drawEnd;
            } else {
                return RenderPoint{
                    value.anchorRect.left + (std::max<LONG>(0, value.anchorRect.right - value.anchorRect.left) / 2),
                    value.anchorRect.top + (std::max<LONG>(0, value.anchorRect.bottom - value.anchorRect.top) / 2)};
            }
        },
        payload);
}
