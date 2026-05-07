#include "widget/impl/vertical_spacer.h"

#include <optional>
#include <string_view>

#include "widget/widget_host.h"

namespace {

std::unique_ptr<Widget> CreateReferencedWidget(std::string_view name) {
    const auto widgetClass = name.empty() ? std::nullopt : EnumFromString<WidgetClass>(name);
    return widgetClass.has_value() ? CreateWidget(*widgetClass) : nullptr;
}

}  // namespace

void VerticalSpacerWidget::Initialize(const LayoutNodeConfig& node) {
    referencedWidgetName_ = node.parameter;
    referencedWidget_.reset();
    if (referencedWidgetName_.empty() || referencedWidgetName_ == "vertical_spacer") {
        return;
    }

    referencedWidget_ = CreateReferencedWidget(referencedWidgetName_);
    if (referencedWidget_ == nullptr) {
        return;
    }

    LayoutNodeConfig referencedNode;
    referencedNode.name = referencedWidgetName_;
    referencedWidget_->Initialize(referencedNode);
}

int VerticalSpacerWidget::PreferredHeight(const WidgetHost& renderer) const {
    return referencedWidget_ != nullptr ? referencedWidget_->PreferredHeight(renderer) : 0;
}
