#include "layout_edit/layout_edit_tooltip_payload.h"

#include <optional>

#include "layout_model/layout_edit_helpers.h"

std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload) {
    if (std::get_if<LayoutEditGuide>(&payload) != nullptr) {
        return std::nullopt;
    }
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&payload)) {
        return guide->parameter;
    }
    if (const auto* color = std::get_if<LayoutEditColorRegion>(&payload)) {
        return color->parameter;
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
        return LayoutEditAnchorParameter(anchor->key);
    }
    if (const auto* gap = std::get_if<LayoutEditGapAnchor>(&payload)) {
        return gap->key.parameter;
    }
    return std::nullopt;
}

std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload) {
    if (std::get_if<LayoutEditGuide>(&payload) != nullptr || std::get_if<LayoutEditColorRegion>(&payload) != nullptr) {
        return std::nullopt;
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
        return LayoutEditAnchorParameter(anchor->key).has_value()
            ? std::optional<double>(static_cast<double>(anchor->value))
            : std::nullopt;
    }
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&payload)) {
        return guide->value;
    }
    if (const auto* gap = std::get_if<LayoutEditGapAnchor>(&payload)) {
        return gap->value;
    }
    return std::nullopt;
}

std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload) {
    if (const auto* guide = std::get_if<LayoutEditGuide>(&payload)) {
        return LayoutWeightEditKey{guide->editCardId, guide->nodePath, guide->separatorIndex};
    }
    if (const auto* guide = std::get_if<LayoutEditWidgetGuide>(&payload)) {
        return guide->parameter;
    }
    if (const auto* color = std::get_if<LayoutEditColorRegion>(&payload)) {
        return color->parameter;
    }
    if (const auto* anchor = std::get_if<LayoutEditAnchorRegion>(&payload)) {
        if (const auto parameter = LayoutEditAnchorParameter(anchor->key); parameter.has_value()) {
            return LayoutEditFocusKey{*parameter};
        }
        if (const auto metricKey = LayoutEditAnchorMetricKey(anchor->key); metricKey.has_value()) {
            return LayoutEditFocusKey{*metricKey};
        }
        if (const auto cardTitleKey = LayoutEditAnchorCardTitleKey(anchor->key); cardTitleKey.has_value()) {
            return LayoutEditFocusKey{*cardTitleKey};
        }
        if (const auto nodeFieldKey = LayoutEditAnchorNodeFieldKey(anchor->key); nodeFieldKey.has_value()) {
            return LayoutEditFocusKey{*nodeFieldKey};
        }
        if (const auto containerOrderKey = LayoutEditAnchorContainerChildOrderKey(anchor->key);
            containerOrderKey.has_value()) {
            return LayoutEditFocusKey{
                LayoutContainerEditKey{containerOrderKey->editCardId, containerOrderKey->nodePath}};
        }
        return std::nullopt;
    }
    if (const auto* gap = std::get_if<LayoutEditGapAnchor>(&payload)) {
        return gap->key.parameter;
    }
    return std::nullopt;
}
