#pragma once

#include "widget/widget.h"

class TextWidget final : public DashboardWidget {
public:
    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    void Draw(DashboardRenderer& renderer,
        const DashboardWidgetLayout& widget,
        const DashboardMetricSource& metrics) const override;
    void BuildEditGuides(DashboardRenderer& renderer, const DashboardWidgetLayout& widget) const override;

private:
    std::string metric_;
    mutable bool staticAnchorRegistered_ = false;
    mutable std::string cachedStaticText_;
};
