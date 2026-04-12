#pragma once

#include "../widget.h"

class DriveUsageListWidget final : public DashboardWidget {
public:
    struct ColumnRects {
        RECT label{};
        RECT read{};
        RECT write{};
        RECT bar{};
        RECT percent{};
        RECT free{};
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
        RECT headerRect{};
        ColumnRects headerColumns{};
        RECT usageHeaderRect{};
        RECT headerReadLabelRect{};
        RECT headerWriteLabelRect{};
        RECT activityTargetRect{};
        RECT activityAnchorRect{};
        std::vector<RECT> rowBands;
        std::vector<ColumnRects> rowColumns;
        std::vector<RECT> rowReadIndicatorRects;
        std::vector<RECT> rowWriteIndicatorRects;
        std::vector<RECT> rowBarRects;
        std::vector<RECT> rowBarAnchorRects;
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
    LayoutState layoutState_{};
};
