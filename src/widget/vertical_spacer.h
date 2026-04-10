#pragma once

#include "../widget.h"

class VerticalSpacerWidget final : public DashboardWidget {
public:
    DashboardWidgetClass Class() const override;
    std::unique_ptr<DashboardWidget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const DashboardRenderer& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    bool IsHoverable() const override;

private:
    std::string referencedWidgetName_;
    std::unique_ptr<DashboardWidget> referencedWidget_;
};
