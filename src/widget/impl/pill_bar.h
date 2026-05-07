#pragma once

#include <optional>

#include "renderer/render_types.h"

class WidgetHost;

std::optional<RenderRect> DrawWidgetPillBar(
    WidgetHost& renderer, const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill);
