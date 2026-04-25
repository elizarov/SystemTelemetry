#pragma once

#include "widget/widget.h"

class NetworkFooterWidget final : public Widget {
public:
    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetHost& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    void BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const override;
    void Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const override;
};
