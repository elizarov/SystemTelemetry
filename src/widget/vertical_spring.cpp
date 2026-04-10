#include "vertical_spring.h"

DashboardWidgetClass VerticalSpringWidget::Class() const {
    return DashboardWidgetClass::VerticalSpring;
}

const char* VerticalSpringWidget::TypeName() const {
    return "vertical_spring";
}

std::unique_ptr<DashboardWidget> VerticalSpringWidget::Clone() const {
    return std::make_unique<VerticalSpringWidget>(*this);
}

void VerticalSpringWidget::Initialize(const LayoutNodeConfig&) {}

int VerticalSpringWidget::PreferredHeight(const DashboardRenderer&) const {
    return 0;
}

bool VerticalSpringWidget::IsHoverable() const {
    return false;
}

bool VerticalSpringWidget::IsVerticalSpring() const {
    return true;
}
