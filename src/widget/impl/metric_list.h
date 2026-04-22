#pragma once

#include <vector>

#include "dashboard/dashboard_metrics.h"
#include "widget/widget.h"

class MetricListWidget final : public DashboardWidget {
public:
    struct LayoutState {
        std::vector<RenderRect> labelRects;
        std::vector<RenderRect> valueRects;
        std::vector<RenderRect> barRects;
        int rowHeight = 0;
        int labelWidth = 1;
        int metricBarHeight = 1;
        int anchorSize = 4;
        int reorderAnchorWidth = 6;
        int reorderAnchorHeight = 10;
        int visibleRows = 0;
        bool showAddRowAnchor = false;
        std::vector<RenderRect> rowRects;
        std::vector<RenderRect> barAnchorRects;
        std::vector<RenderRect> reorderAnchorRects;
        RenderRect addRowRect{};
        RenderRect addRowAnchorRect{};
    };

    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    void ResolveLayoutState(const DashboardRenderer& renderer, const RenderRect& rect) override;
    void Draw(DashboardRenderer& renderer,
        const DashboardWidgetLayout& widget,
        const DashboardMetricSource& metrics) const override;
    void BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::vector<std::string> metricRefs_;
    LayoutState layoutState_{};
};
