#pragma once

#include <string>

#include "widget/card_chrome_layout.h"
#include "widget/layout_edit_types.h"
#include "widget/widget.h"

class CardChromeWidget : public Widget {
public:
    CardChromeWidget() = default;
    explicit CardChromeWidget(const LayoutCardConfig& card);

    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetRenderer& renderer) const override;
    bool IsHoverable() const override;
    void ResolveLayoutState(const WidgetRenderer& renderer, const RenderRect& rect) override;
    void Draw(WidgetRenderer& renderer, const WidgetLayout& widget, const MetricSource& metrics) const override;
    void BuildEditGuides(WidgetRenderer& renderer, const WidgetLayout& widget) const override;
    void BuildStaticAnchors(WidgetRenderer& renderer, const WidgetLayout& widget) const override;

private:
    static LayoutEditWidgetIdentity CardIdentity(const WidgetLayout& widget);

    std::string title_;
    std::string iconName_;
    CardChromeLayout layoutState_{};
};
