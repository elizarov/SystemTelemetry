#pragma once

#include "widget/widget.h"

class ThroughputWidget final : public DashboardWidget {
public:
    struct LayoutState {
        RenderRect valueRect{};
        RenderRect graphRect{};
        RenderRect leaderAnchorRect{};
        RenderRect plotAnchorRect{};
        RenderRect guideAnchorRect{};
        int axisWidth = 1;
        int labelBandHeight = 0;
        int graphTop = 0;
        int graphLeft = 0;
        int graphRight = 0;
        int graphBottom = 0;
        int plotTop = 0;
        int plotHeight = 1;
        int plotStrokeWidth = 1;
        int guideStrokeWidth = 1;
        int leaderDiameter = 0;
        int leaderRadius = 0;
        int plotWidth = 1;
        int guideCenterX = 0;
        int guideCenterY = 0;
        int leaderAnchorCenterX = 0;
        int leaderAnchorCenterY = 0;
        int plotAnchorCenterX = 0;
        int plotAnchorCenterY = 0;
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
    std::string metric_;
    LayoutState layoutState_{};
};
