#pragma once

#include "../widget.h"

class VerticalSpringWidget final : public DashboardWidget {
public:
    const char* TypeName() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    bool IsHoverable() const override;
    bool IsVerticalSpring() const override;
};
