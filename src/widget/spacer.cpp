#include "spacer.h"

#include "../dashboard_renderer.h"

const char* SpacerWidget::TypeName() const {
    return "spacer";
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
