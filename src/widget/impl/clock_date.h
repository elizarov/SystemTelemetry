#pragma once

#include "widget/widget.h"

class ClockDateWidget final : public DashboardWidget {
public:
    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetRenderer& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    void Draw(
        WidgetRenderer& renderer, const DashboardWidgetLayout& widget, const MetricSource& metrics) const override;
};
