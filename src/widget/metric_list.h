#pragma once

#include <vector>

#include "../widget.h"

class MetricListWidget final : public DashboardWidget {
public:
    struct LayoutState {
        std::vector<RECT> labelRects;
        std::vector<RECT> valueRects;
        std::vector<RECT> barRects;
        int rowHeight = 0;
        int labelWidth = 1;
        int metricBarHeight = 1;
        int anchorSize = 4;
        int visibleRows = 0;
        std::vector<RECT> rowRects;
        std::vector<RECT> barAnchorRects;
    };

    struct Entry {
        std::string metricRef;
        std::string labelOverride;
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
    void BuildStaticAnchors(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::vector<Entry> entries_;
    LayoutState layoutState_{};
};
