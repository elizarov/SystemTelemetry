#include "widget/impl/vertical_spring.h"

WidgetClass VerticalSpringWidget::Class() const {
    return WidgetClass::VerticalSpring;
}

std::unique_ptr<Widget> VerticalSpringWidget::Clone() const {
    return std::make_unique<VerticalSpringWidget>(*this);
}

void VerticalSpringWidget::Initialize(const LayoutNodeConfig&) {}

int VerticalSpringWidget::PreferredHeight(const WidgetHost&) const {
    return 0;
}

bool VerticalSpringWidget::IsHoverable() const {
    return false;
}

bool VerticalSpringWidget::IsVerticalSpring() const {
    return true;
}
