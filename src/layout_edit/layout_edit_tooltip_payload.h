#pragma once

#include <optional>

#include "widget/layout_edit_types.h"

bool IsLayoutGuidePayload(const TooltipPayload& payload);
std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload);
std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload);
std::optional<unsigned int> TooltipPayloadColorValue(const TooltipPayload& payload);
RenderPoint TooltipPayloadAnchorPoint(const TooltipPayload& payload);
std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload);
