#include "layout_edit_types.h"

#include <algorithm>
#include <type_traits>

bool MatchesWidgetIdentity(const LayoutEditWidgetIdentity& left, const LayoutEditWidgetIdentity& right) {
    return left.kind == right.kind && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath;
}

bool MatchesParameterSubject(const LayoutEditParameterSubject& left, const LayoutEditParameterSubject& right) {
    return left.parameter == right.parameter && MatchesWidgetIdentity(left.widget, right.widget);
}

bool MatchesLayoutEditGuide(const LayoutEditGuide& left, const LayoutEditGuide& right) {
    return left.axis == right.axis && left.renderCardId == right.renderCardId && left.editCardId == right.editCardId &&
           left.nodePath == right.nodePath && left.separatorIndex == right.separatorIndex;
}

bool MatchesGapEditAnchorKey(const LayoutEditGapAnchorKey& left, const LayoutEditGapAnchorKey& right) {
    return MatchesParameterSubject(left, right) && left.nodePath == right.nodePath;
}

bool MatchesEditableAnchorKey(const LayoutEditAnchorKey& left, const LayoutEditAnchorKey& right) {
    return left.anchorId == right.anchorId && MatchesParameterSubject(left, right);
}

bool MatchesWidgetEditGuide(const LayoutEditWidgetGuide& left, const LayoutEditWidgetGuide& right) {
    return left.axis == right.axis && left.guideId == right.guideId && MatchesParameterSubject(left, right);
}

bool MatchesLayoutContainerEditKey(const LayoutContainerEditKey& left, const LayoutContainerEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath;
}

bool MatchesLayoutWeightEditKey(const LayoutWeightEditKey& left, const LayoutWeightEditKey& right) {
    return left.editCardId == right.editCardId && left.nodePath == right.nodePath &&
           left.separatorIndex == right.separatorIndex;
}

bool MatchesCardChromeSelectionIdentity(
    const LayoutEditWidgetIdentity& selection, const LayoutEditWidgetIdentity& candidate) {
    return selection.kind == LayoutEditWidgetIdentity::Kind::CardChrome &&
           candidate.kind == LayoutEditWidgetIdentity::Kind::CardChrome && selection.editCardId == candidate.editCardId;
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& left, const LayoutEditFocusKey& right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (const auto* leftParameter = std::get_if<LayoutEditParameter>(&left)) {
        return *leftParameter == std::get<LayoutEditParameter>(right);
    }
    return MatchesLayoutWeightEditKey(std::get<LayoutWeightEditKey>(left), std::get<LayoutWeightEditKey>(right));
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGuide& guide) {
    const auto* weightKey = std::get_if<LayoutWeightEditKey>(&focusKey);
    return weightKey != nullptr && MatchesLayoutWeightEditKey(*weightKey,
                                       LayoutWeightEditKey{guide.editCardId, guide.nodePath, guide.separatorIndex});
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditWidgetGuide& guide) {
    const auto* parameter = std::get_if<LayoutEditParameter>(&focusKey);
    return parameter != nullptr && *parameter == guide.parameter;
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditGapAnchorKey& key) {
    const auto* parameter = std::get_if<LayoutEditParameter>(&focusKey);
    return parameter != nullptr && *parameter == key.parameter;
}

bool MatchesLayoutEditFocusKey(const LayoutEditFocusKey& focusKey, const LayoutEditAnchorKey& key) {
    const auto* parameter = std::get_if<LayoutEditParameter>(&focusKey);
    return parameter != nullptr && *parameter == key.parameter;
}

bool MatchesLayoutEditSelectionHighlight(const LayoutEditSelectionHighlight& highlight, const LayoutEditGuide& guide) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, guide);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditWidgetGuide& guide) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, guide);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditGapAnchorKey& key) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, key);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditAnchorKey& key) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    return focusKey != nullptr && MatchesLayoutEditFocusKey(*focusKey, key);
}

bool MatchesLayoutEditSelectionHighlight(
    const LayoutEditSelectionHighlight& highlight, const LayoutEditColorRegion& region) {
    const auto* focusKey = std::get_if<LayoutEditFocusKey>(&highlight);
    const auto* parameter = focusKey != nullptr ? std::get_if<LayoutEditParameter>(focusKey) : nullptr;
    return parameter != nullptr && *parameter == region.parameter;
}

bool IsLayoutGuidePayload(const TooltipPayload& payload) {
    return std::holds_alternative<LayoutEditGuide>(payload);
}

std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<LayoutEditParameter> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide>) {
                return value.parameter;
            } else if constexpr (std::is_same_v<T, LayoutEditColorRegion>) {
                return value.parameter;
            } else {
                return value.key.parameter;
            }
        },
        payload);
}

std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<double> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, LayoutEditColorRegion>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, LayoutEditAnchorRegion>) {
                return static_cast<double>(value.value);
            } else {
                return value.value;
            }
        },
        payload);
}

std::optional<unsigned int> TooltipPayloadColorValue(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<unsigned int> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditColorRegion>) {
                return std::nullopt;
            } else {
                return std::nullopt;
            }
        },
        payload);
}

RenderPoint TooltipPayloadAnchorPoint(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> RenderPoint {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return RenderPoint{
                    value.lineRect.left + (std::max<LONG>(0, value.lineRect.right - value.lineRect.left) / 2),
                    value.lineRect.top + (std::max<LONG>(0, value.lineRect.bottom - value.lineRect.top) / 2)};
            } else if constexpr (std::is_same_v<T, LayoutEditGapAnchor>) {
                return RenderPoint{
                    value.handleRect.left + (std::max<LONG>(0, value.handleRect.right - value.handleRect.left) / 2),
                    value.handleRect.top + (std::max<LONG>(0, value.handleRect.bottom - value.handleRect.top) / 2)};
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide>) {
                return value.drawEnd;
            } else if constexpr (std::is_same_v<T, LayoutEditColorRegion>) {
                return value.targetRect.Center();
            } else {
                return RenderPoint{
                    value.anchorRect.left + (std::max<LONG>(0, value.anchorRect.right - value.anchorRect.left) / 2),
                    value.anchorRect.top + (std::max<LONG>(0, value.anchorRect.bottom - value.anchorRect.top) / 2)};
            }
        },
        payload);
}

std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<LayoutEditFocusKey> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return LayoutWeightEditKey{value.editCardId, value.nodePath, value.separatorIndex};
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide>) {
                return value.parameter;
            } else if constexpr (std::is_same_v<T, LayoutEditColorRegion>) {
                return value.parameter;
            } else {
                return value.key.parameter;
            }
        },
        payload);
}
