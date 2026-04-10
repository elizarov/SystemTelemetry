#include "vertical_spacer.h"

#include "../dashboard_renderer.h"

DashboardWidgetClass VerticalSpacerWidget::Class() const {
    return DashboardWidgetClass::VerticalSpacer;
}

std::unique_ptr<DashboardWidget> VerticalSpacerWidget::Clone() const {
    auto widget = std::make_unique<VerticalSpacerWidget>();
    widget->referencedWidgetName_ = referencedWidgetName_;
    if (!referencedWidgetName_.empty()) {
        widget->referencedWidget_ = CreateDashboardWidget(referencedWidgetName_);
        if (widget->referencedWidget_ != nullptr) {
            LayoutNodeConfig node;
            node.name = referencedWidgetName_;
            widget->referencedWidget_->Initialize(node);
        }
    }
    return widget;
}

void VerticalSpacerWidget::Initialize(const LayoutNodeConfig& node) {
    referencedWidgetName_ = node.parameter;
    referencedWidget_.reset();
    if (referencedWidgetName_.empty() || referencedWidgetName_ == "vertical_spacer") {
        return;
    }

    referencedWidget_ = CreateDashboardWidget(referencedWidgetName_);
    if (referencedWidget_ == nullptr) {
        return;
    }

    LayoutNodeConfig referencedNode;
    referencedNode.name = referencedWidgetName_;
    referencedWidget_->Initialize(referencedNode);
}

int VerticalSpacerWidget::PreferredHeight(const DashboardRenderer& renderer) const {
    return referencedWidget_ != nullptr ? referencedWidget_->PreferredHeight(renderer) : 0;
}

bool VerticalSpacerWidget::UsesFixedPreferredHeightInRows() const {
    return true;
}

bool VerticalSpacerWidget::IsHoverable() const {
    return false;
}
