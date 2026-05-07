#pragma once

#include "widget/widget.h"

class TextWidget final : public Widget {
public:
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetHost& renderer) const override;
    void Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const override;
    void BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const override;

private:
    std::string metric_;
};
