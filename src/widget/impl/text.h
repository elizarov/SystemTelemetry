#pragma once

#include "widget/widget.h"

class TextWidget final : public Widget {
public:
    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetRenderer& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    void Draw(WidgetRenderer& renderer, const WidgetLayout& widget, const MetricSource& metrics) const override;
    void BuildEditGuides(WidgetRenderer& renderer, const WidgetLayout& widget) const override;

private:
    std::string metric_;
    mutable bool staticAnchorRegistered_ = false;
    mutable std::string cachedStaticText_;
};
