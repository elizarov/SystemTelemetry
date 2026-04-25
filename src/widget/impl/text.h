#pragma once

#include "widget/widget.h"

class TextWidget final : public Widget {
public:
    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetHost& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    void Draw(WidgetHost& renderer, const WidgetLayout& widget, const MetricSource& metrics) const override;
    void BuildEditGuides(WidgetHost& renderer, const WidgetLayout& widget) const override;

private:
    std::string metric_;
    mutable bool staticAnchorRegistered_ = false;
    mutable std::string cachedStaticText_;
};
