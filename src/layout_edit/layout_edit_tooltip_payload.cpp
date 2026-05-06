#include "layout_edit/layout_edit_tooltip_payload.h"

#include <optional>
#include <type_traits>

#include "layout_model/layout_edit_helpers.h"

std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<LayoutEditParameter> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide> || std::is_same_v<T, LayoutEditColorRegion>) {
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
            if constexpr (std::is_same_v<T, LayoutEditGuide> || std::is_same_v<T, LayoutEditColorRegion>) {
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

std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload) {
    return std::visit(
        [](const auto& value) -> std::optional<LayoutEditFocusKey> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LayoutEditGuide>) {
                return LayoutWeightEditKey{value.editCardId, value.nodePath, value.separatorIndex};
            } else if constexpr (std::is_same_v<T, LayoutEditWidgetGuide> || std::is_same_v<T, LayoutEditColorRegion>) {
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
                if (const auto nodeFieldKey = LayoutEditAnchorNodeFieldKey(value.key); nodeFieldKey.has_value()) {
                    return LayoutEditFocusKey{*nodeFieldKey};
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
