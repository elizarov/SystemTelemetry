#pragma once

#include <optional>

#include "widget/layout_edit_types.h"

std::optional<LayoutEditParameter> TooltipPayloadParameter(const TooltipPayload& payload);
std::optional<double> TooltipPayloadNumericValue(const TooltipPayload& payload);
std::optional<LayoutEditFocusKey> TooltipPayloadFocusKey(const TooltipPayload& payload);
