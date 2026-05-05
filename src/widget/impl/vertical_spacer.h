#pragma once

#include "widget/widget.h"

class VerticalSpacerWidget final : public Widget {
public:
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetHost& renderer) const override;

private:
    std::string referencedWidgetName_;
    std::unique_ptr<Widget> referencedWidget_;
};
