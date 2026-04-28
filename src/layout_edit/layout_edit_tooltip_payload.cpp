#include "layout_edit/layout_edit_tooltip_payload.h"

#include <algorithm>
#include <type_traits>

#include "layout_model/layout_edit_helpers.h"

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
            } else if constexpr (std::is_same_v<T, LayoutEditAnchorRegion>) {
                return LayoutEditAnchorParameter(value.key);
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
                return LayoutEditAnchorParameter(value.key).has_value()
                           ? std::optional<double>(static_cast<double>(value.value))
                           : std::nullopt;
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
                    value.lineRect.left + (std::max<int>(0, value.lineRect.right - value.lineRect.left) / 2),
                    value.lineRect.top + (std::max<int>(0, value.lineRect.bottom - value.lineRect.top) / 2)};
            } else if constexpr (std::is_same_v<T, LayoutEditGapAnchor>) {
                return RenderPoint{
                    value.handleRect.left + (std::max<int>(0, value.handleRect.right - value.handleRect.left) / 2),
                    value.handleRect.top + (std::max<int>(0, value.handleRect.bottom - value.handleRect.top) / 2)};
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide>) {
                return value.drawEnd;
            } else if constexpr (std::is_same_v<T, LayoutEditColorRegion>) {
                return value.targetRect.Center();
            } else {
                return RenderPoint{
                    value.anchorRect.left + (std::max<int>(0, value.anchorRect.right - value.anchorRect.left) / 2),
                    value.anchorRect.top + (std::max<int>(0, value.anchorRect.bottom - value.anchorRect.top) / 2)};
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
            } else if constexpr (std::is_same_v<T, LayoutEditAnchorRegion>) {
                if (const auto parameter = LayoutEditAnchorParameter(value.key); parameter.has_value()) {
                    return LayoutEditFocusKey{*parameter};
                }
                if (const auto metricKey = LayoutEditAnchorMetricKey(value.key); metricKey.has_value()) {
                    return LayoutEditFocusKey{*metricKey};
                }
                if (const auto cardTitleKey = LayoutEditAnchorCardTitleKey(value.key); cardTitleKey.has_value()) {
                    return LayoutEditFocusKey{*cardTitleKey};
                }
                if (const auto metricListOrderKey = LayoutEditAnchorMetricListOrderKey(value.key);
                    metricListOrderKey.has_value()) {
                    return LayoutEditFocusKey{*metricListOrderKey};
                }
                if (const auto dateTimeFormatKey = LayoutEditAnchorDateTimeFormatKey(value.key);
                    dateTimeFormatKey.has_value()) {
                    return LayoutEditFocusKey{*dateTimeFormatKey};
                }
                if (const auto containerOrderKey = LayoutEditAnchorContainerChildOrderKey(value.key);
                    containerOrderKey.has_value()) {
                    return LayoutEditFocusKey{
                        LayoutContainerEditKey{containerOrderKey->editCardId, containerOrderKey->nodePath}};
                }
                return std::nullopt;
            } else {
                return value.key.parameter;
            }
        },
        payload);
}
