#include "spacer.h"

#include "../dashboard_renderer.h"

DashboardWidgetClass SpacerWidget::Class() const {
    return DashboardWidgetClass::Spacer;
}

std::unique_ptr<DashboardWidget> SpacerWidget::Clone() const {
    return std::make_unique<SpacerWidget>(*this);
}

void SpacerWidget::Initialize(const LayoutNodeConfig&) {}

int SpacerWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return renderer.FontMetrics().footer +
           (std::max)(0, renderer.ScaleLogical(renderer.Config().layout.networkFooter.preferredPadding));
}

bool SpacerWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

bool SpacerWidget::IsHoverable() const {
    return false;
}
