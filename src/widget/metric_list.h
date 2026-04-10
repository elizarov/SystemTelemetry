#pragma once

#include <vector>

#include "../widget.h"

class MetricListWidget final : public DashboardWidget {
public:
    struct Entry {
        std::string metricRef;
        std::string labelOverride;
    };

    const char* TypeName() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    void Draw(
        DashboardRenderer& renderer, HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::vector<Entry> entries_;
};
