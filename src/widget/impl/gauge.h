#pragma once

#include <memory>
#include <vector>

#include "widget/widget.h"

struct GaugeSharedLayout;

class GaugeWidget final : public DashboardWidget {
public:
    struct SegmentLayout {
        int segmentCount = 1;
        double totalSweep = 0.0;
        double gapSweep = 360.0;
        double segmentGap = 0.0;
        double segmentSweep = 0.0;
        double pitchSweep = 0.0;
        double gaugeStart = 90.0;
        double gaugeEnd = 90.0;
        double maxSegmentSweep = 0.0;
    };

    struct LayoutState {
        SegmentLayout segmentLayout{};
        int outerRadius = 0;
        int cx = 0;
        int cy = 0;
        int ringThickness = 1;
        int innerRadius = 0;
        int anchorPadding = 1;
        int anchorSize = 4;
        int anchorHalf = 2;
        int halfWidth = 1;
        int valueBottom = 0;
        int valueHeight = 0;
        int labelBottom = 0;
        int labelHeight = 0;
        int guideHalfExtension = 1;
        int hitInset = 4;
        RenderRect segmentCountAnchorRect{};
        RenderRect outerPaddingAnchorRect{};
        RenderRect ringThicknessAnchorRect{};
        RenderRect valueRect{};
        RenderRect labelRect{};
        std::vector<RenderRingSegment> ringSegments;
    };

    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetRenderer& renderer) const override;
    void ResolveLayoutState(const WidgetRenderer& renderer, const RenderRect& rect) override;
    void Draw(
        WidgetRenderer& renderer, const DashboardWidgetLayout& widget, const MetricSource& metrics) const override;
    void FinalizeLayoutGroup(WidgetRenderer& renderer, const std::vector<DashboardWidgetLayout*>& widgets) override;
    void BuildStaticAnchors(WidgetRenderer& renderer, const DashboardWidgetLayout& widget) const override;
    void BuildEditGuides(WidgetRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::string metric_;
    std::shared_ptr<GaugeSharedLayout> sharedLayout_;
    LayoutState layoutState_{};
};
