#pragma once

#include <memory>
#include <vector>

#include "../widget.h"

struct GaugeSharedLayout;

namespace Gdiplus {
class GraphicsPath;
}

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
        RECT segmentCountAnchorRect{};
        RECT outerPaddingAnchorRect{};
        RECT ringThicknessAnchorRect{};
        RECT valueRect{};
        RECT labelRect{};
        std::vector<std::shared_ptr<Gdiplus::GraphicsPath>> segmentPaths;
    };

    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    void ResolveLayoutState(const DashboardRenderer& renderer, const RECT& rect) override;
    void Draw(DashboardRenderer& renderer,
        HDC hdc,
        const DashboardWidgetLayout& widget,
        const DashboardMetricSource& metrics) const override;
    void FinalizeLayoutGroup(DashboardRenderer& renderer, const std::vector<DashboardWidgetLayout*>& widgets) override;
    void BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::string metric_;
    std::shared_ptr<GaugeSharedLayout> sharedLayout_;
    LayoutState layoutState_{};
};
