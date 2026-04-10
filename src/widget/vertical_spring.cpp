#include "vertical_spring.h"

const char* VerticalSpringWidget::TypeName() const {
    return "vertical_spring";
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
