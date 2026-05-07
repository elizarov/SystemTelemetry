#pragma once

#include "widget/widget.h"

class VerticalSpringWidget final : public Widget {
public:
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetHost& renderer) const override;
};
