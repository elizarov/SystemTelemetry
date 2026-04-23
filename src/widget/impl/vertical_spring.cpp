#include "widget/impl/vertical_spring.h"

DashboardWidgetClass VerticalSpringWidget::Class() const {
    return DashboardWidgetClass::VerticalSpring;
}

std::unique_ptr<DashboardWidget> VerticalSpringWidget::Clone() const {
    return std::make_unique<VerticalSpringWidget>(*this);
}

void VerticalSpringWidget::Initialize(const LayoutNodeConfig&) {}

int VerticalSpringWidget::PreferredHeight(const WidgetRenderer&) const {
    return 0;
}

bool VerticalSpringWidget::IsHoverable() const {
    return false;
}

bool VerticalSpringWidget::IsVerticalSpring() const {
    return true;
}
