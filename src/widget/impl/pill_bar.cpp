#include "widget/impl/pill_bar.h"

#include <algorithm>
#include <cmath>

#include "util/numeric_safety.h"
#include "widget/widget_host.h"

namespace {

void FillPill(WidgetHost& renderer, const RenderRect& rect, RenderColorId color) {
    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width <= height) {
        renderer.Renderer().FillSolidEllipse(rect, color);
    } else {
        renderer.Renderer().FillSolidRoundedRect(rect, height / 2, color);
    }
}

}  // namespace

std::optional<RenderRect> DrawWidgetPillBar(
    WidgetHost& renderer, const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    FillPill(renderer, rect, RenderColorId::Track);

    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0 || !drawFill) {
        return std::nullopt;
    }

    const double clampedRatio = ClampFinite(ratio, 0.0, 1.0);
    const int straightWidth = std::max(0, width - height);
    const int fillWidth = std::min(width, height + static_cast<int>(std::round(clampedRatio * straightWidth)));
    RenderRect fillRect = rect;
    fillRect.right = fillRect.left + fillWidth;
    FillPill(renderer, fillRect, RenderColorId::Accent);

    if (!peakRatio.has_value()) {
        return std::nullopt;
    }

    const double peak = ClampFinite(*peakRatio, 0.0, 1.0);
    const int markerWidth = std::min(width, std::max(1, std::max(renderer.Renderer().ScaleLogical(4), height)));
    const int centerX = rect.left + static_cast<int>(std::round(peak * width));
    const int minLeft = rect.left;
    const int maxLeft = rect.right - markerWidth;
    const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
    RenderRect markerRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
    FillPill(renderer, markerRect, RenderColorId::PeakGhost);
    return markerRect;
}
