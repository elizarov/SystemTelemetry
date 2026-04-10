#pragma once

#include "../widget.h"

class SpacerWidget final : public DashboardWidget {
public:
    const char* TypeName() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    bool IsHoverable() const override;
};
