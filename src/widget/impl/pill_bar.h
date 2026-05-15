#pragma once

#include <optional>

#include "renderer/render_types.h"
#include "widget/animation.h"
#include "widget/impl/animation_primitives.h"

class Renderer;
class WidgetHost;

void DrawWidgetPillBarTrack(Renderer& renderer, const RenderRect& rect);
std::optional<RenderRect> DrawWidgetPillBarAnimated(
    Renderer& renderer, const RenderRect& rect, const ScalarFillSample& sample);
std::optional<RenderRect> DrawWidgetPillBar(
    WidgetHost& renderer, const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill);
std::optional<RenderRect> DrawWidgetPillBar(
    WidgetHost& renderer, const RenderRect& rect, const ScalarFillSample& sample);
std::optional<RenderRect> WidgetPillBarPeakMarkerRect(
    const Renderer& renderer, const RenderRect& rect, const ScalarFillSample& sample);
WidgetAnimationPtr MakeWidgetPillBarAnimation(AnimationDataKey key, RenderRect rect);
