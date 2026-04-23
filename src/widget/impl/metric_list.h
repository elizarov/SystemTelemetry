#pragma once

#include <vector>

#include "telemetry/metrics.h"
#include "widget/widget.h"

class MetricListWidget final : public Widget {
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

    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetRenderer& renderer) const override;
    void ResolveLayoutState(const WidgetRenderer& renderer, const RenderRect& rect) override;
    void Draw(WidgetRenderer& renderer, const WidgetLayout& widget, const MetricSource& metrics) const override;
    void BuildStaticAnchors(WidgetRenderer& renderer, const WidgetLayout& widget) const override;
    void BuildEditGuides(WidgetRenderer& renderer, const WidgetLayout& widget) const override;

private:
    std::vector<std::string> metricRefs_;
    LayoutState layoutState_{};
};
