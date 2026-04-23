#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/config.h"
#include "config/widget_class.h"
#include "widget/render_types.h"

class MetricSource;
class WidgetRenderer;

class Widget {
public:
    virtual ~Widget() = default;

    virtual WidgetClass Class() const = 0;
    virtual std::unique_ptr<Widget> Clone() const = 0;
    virtual void Initialize(const LayoutNodeConfig& node) = 0;
    virtual int PreferredHeight(const WidgetRenderer& renderer) const = 0;
    virtual bool UsesFixedPreferredHeightInRows() const;
    virtual bool IsHoverable() const;
    virtual bool IsVerticalSpring() const;
    virtual void ResolveLayoutState(const WidgetRenderer& renderer, const RenderRect& rect);
    virtual void Draw(WidgetRenderer& renderer, const struct WidgetLayout& widget, const MetricSource& metrics) const;
    virtual void FinalizeLayoutGroup(WidgetRenderer& renderer, const std::vector<struct WidgetLayout*>& widgets);
    virtual void BuildEditGuides(WidgetRenderer& renderer, const struct WidgetLayout& widget) const;
    virtual void BuildStaticAnchors(WidgetRenderer& renderer, const struct WidgetLayout& widget) const;
};

struct WidgetLayout {
    RenderRect rect{};
    std::string cardId;
    std::string editCardId;
    std::vector<size_t> nodePath;
    std::unique_ptr<Widget> widget;
};

std::unique_ptr<Widget> CreateWidget(WidgetClass widgetClass);
std::unique_ptr<Widget> CreateWidget(std::string_view name);
