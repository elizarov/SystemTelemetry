#include "widget/impl/pill_bar.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "renderer/renderer.h"
#include "util/numeric_safety.h"
#include "widget/widget_host.h"

namespace {

void FillPill(Renderer& renderer, const RenderRect& rect, RenderColorId color) {
    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width <= height) {
        renderer.FillSolidEllipse(rect, color);
    } else {
        renderer.FillSolidRoundedRect(rect, height / 2, color);
    }
}

class WidgetPillBarAnimation final : public WidgetAnimation {
public:
    WidgetPillBarAnimation(AnimationDataKey key, RenderRect rect) :
        key_(std::move(key)),
        rect_(rect) {}

    const AnimationDataKey& Key() const override {
        return key_;
    }

    RenderRect DirtyBounds() const override {
        return rect_;
    }

    void Draw(Renderer& renderer, const WidgetAnimationState& state) const override {
        DrawWidgetPillBarAnimated(renderer, rect_, ScalarFillSampleFromState(state));
    }

private:
    AnimationDataKey key_;
    RenderRect rect_{};
};

}  // namespace

void DrawWidgetPillBarTrack(Renderer& renderer, const RenderRect& rect) {
    FillPill(renderer, rect, RenderColorId::Track);
}

std::optional<RenderRect> WidgetPillBarPeakMarkerRect(
    const Renderer& renderer, const RenderRect& rect, const ScalarFillSample& sample) {
    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0 || !sample.peakRatio.has_value()) {
        return std::nullopt;
    }

    const double peak = ClampFinite(*sample.peakRatio, 0.0, 1.0);
    const int markerWidth = std::min(width, std::max(1, std::max(renderer.ScaleLogical(4), height)));
    const int centerX = rect.left + static_cast<int>(std::round(peak * width));
    const int minLeft = rect.left;
    const int maxLeft = rect.right - markerWidth;
    const int markerLeft = std::clamp(centerX - markerWidth / 2, minLeft, maxLeft);
    return RenderRect{markerLeft, rect.top, markerLeft + markerWidth, rect.bottom};
}

std::optional<RenderRect> DrawWidgetPillBarAnimated(
    Renderer& renderer, const RenderRect& rect, const ScalarFillSample& sample) {
    const int width = rect.Width();
    const int height = rect.Height();
    if (width <= 0 || height <= 0 || !sample.valueRatio.has_value()) {
        return std::nullopt;
    }

    const double clampedRatio = ClampFinite(*sample.valueRatio, 0.0, 1.0);
    const int straightWidth = std::max(0, width - height);
    const int fillWidth = std::min(width, height + static_cast<int>(std::round(clampedRatio * straightWidth)));
    RenderRect fillRect = rect;
    fillRect.right = fillRect.left + fillWidth;
    FillPill(renderer, fillRect, RenderColorId::Accent);

    const std::optional<RenderRect> markerRect = WidgetPillBarPeakMarkerRect(renderer, rect, sample);
    if (markerRect.has_value()) {
        FillPill(renderer, *markerRect, RenderColorId::PeakGhost);
    }
    return markerRect;
}

std::optional<RenderRect> DrawWidgetPillBar(
    WidgetHost& renderer, const RenderRect& rect, double ratio, std::optional<double> peakRatio, bool drawFill) {
    if (!drawFill) {
        DrawWidgetPillBarTrack(renderer.Renderer(), rect);
        return std::nullopt;
    }

    ScalarFillSample sample;
    sample.valueRatio = ratio;
    sample.peakRatio = peakRatio;
    return DrawWidgetPillBar(renderer, rect, sample);
}

std::optional<RenderRect> DrawWidgetPillBar(
    WidgetHost& renderer, const RenderRect& rect, const ScalarFillSample& sample) {
    DrawWidgetPillBarTrack(renderer.Renderer(), rect);
    return DrawWidgetPillBarAnimated(renderer.Renderer(), rect, sample);
}

WidgetAnimationPtr MakeWidgetPillBarAnimation(AnimationDataKey key, RenderRect rect) {
    return std::make_unique<WidgetPillBarAnimation>(std::move(key), rect);
}
