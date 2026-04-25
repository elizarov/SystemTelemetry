#pragma once

#include "widget/widget.h"

class VerticalSpacerWidget final : public Widget {
public:
    WidgetClass Class() const override;
    std::unique_ptr<Widget> Clone() const override;
    void Initialize(const LayoutNodeConfig& node) override;
    int PreferredHeight(const WidgetHost& renderer) const override;
    bool UsesFixedPreferredHeightInRows() const override;
    bool IsHoverable() const override;

private:
    std::string referencedWidgetName_;
    std::unique_ptr<Widget> referencedWidget_;
};
