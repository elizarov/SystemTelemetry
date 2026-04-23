#pragma once

#include "widget/widget.h"

class VerticalSpringWidget final : public Widget {
public:
    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetRenderer& renderer) const override;
    bool IsHoverable() const override;
    bool IsVerticalSpring() const override;
};
