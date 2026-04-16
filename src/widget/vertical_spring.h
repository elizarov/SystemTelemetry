#pragma once

#include "widget.h"

class VerticalSpringWidget final : public DashboardWidget {
public:
    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    bool IsHoverable() const override;
    bool IsVerticalSpring() const override;
};
