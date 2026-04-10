#pragma once

#include <memory>

#include "../widget.h"

struct GaugeSharedLayout;

class GaugeWidget final : public DashboardWidget {
public:
    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    void Draw(DashboardRenderer& renderer,
        HDC hdc,
        const DashboardWidgetLayout& widget,
        const DashboardMetricSource& metrics) const override;
    void FinalizeLayoutGroup(DashboardRenderer& renderer, const std::vector<DashboardWidgetLayout*>& widgets) override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::string metric_;
    std::shared_ptr<GaugeSharedLayout> sharedLayout_;
};
