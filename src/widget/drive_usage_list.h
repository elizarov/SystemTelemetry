#pragma once

#include "../widget.h"

class DriveUsageListWidget final : public DashboardWidget {
public:
    struct MeasuredColumnWidths {
        int label = 1;
        int percent = 1;
    };

    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    void ResolveLayoutState(const DashboardRenderer& renderer) override;
    void Draw(DashboardRenderer& renderer,
        HDC hdc,
        const DashboardWidgetLayout& widget,
        const DashboardMetricSource& metrics) const override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    MeasuredColumnWidths measuredColumnWidths_{};
};
