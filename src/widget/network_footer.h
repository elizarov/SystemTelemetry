#pragma once

#include "../widget.h"

class NetworkFooterWidget final : public DashboardWidget {
public:
    const char* TypeName() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    void Draw(
        DashboardRenderer& renderer, HDC hdc, const DashboardWidgetLayout& widget, const DashboardMetricSource& metrics) const override;
};
