#pragma once

#include "../widget.h"

class DriveUsageListWidget final : public DashboardWidget {
public:
    struct ColumnRects {
        RenderRect label{};
        RenderRect read{};
        RenderRect write{};
        RenderRect bar{};
        RenderRect percent{};
        RenderRect free{};
    };

    struct MeasuredColumnWidths {
        int label = 1;
        int percent = 1;
    };

    struct LayoutState {
        MeasuredColumnWidths measuredColumnWidths{};
        int headerHeight = 0;
        int rowHeight = 0;
        int labelGap = 0;
        int activityWidth = 1;
        int rwGap = 0;
        int barGap = 0;
        int percentGap = 0;
        int freeWidth = 1;
        int driveBarHeight = 1;
        int activitySegments = 1;
        int activitySegmentGap = 0;
        int rowContentHeight = 1;
        int activityAnchorSize = 8;
        int activityAnchorCenterX = 0;
        int firstRowContentTop = 0;
        int visibleRows = 0;
        int clampedActivitySegmentGap = 0;
        int lowestSegmentTop = 0;
        RenderRect headerRect{};
        ColumnRects headerColumns{};
        RenderRect usageHeaderRect{};
        RenderRect headerReadLabelRect{};
        RenderRect headerWriteLabelRect{};
        RenderRect activityTargetRect{};
        RenderRect activityAnchorRect{};
        std::vector<RenderRect> rowBands;
        std::vector<ColumnRects> rowColumns;
        std::vector<RenderRect> rowReadIndicatorRects;
        std::vector<RenderRect> rowWriteIndicatorRects;
        std::vector<RenderRect> rowBarRects;
        std::vector<RenderRect> rowBarAnchorRects;
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
    LayoutState layoutState_{};
};
