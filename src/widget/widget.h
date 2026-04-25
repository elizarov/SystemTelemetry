#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "config/widget_class.h"
#include "renderer/render_types.h"

class MetricSource;
class WidgetHost;

class Widget {
public:
    virtual ~Widget() = default;

    virtual WidgetClass Class() const = 0;
    virtual std::unique_ptr<Widget> Clone() const = 0;
    virtual void Initialize(const LayoutNodeConfig& node) = 0;
    virtual int PreferredHeight(const WidgetHost& renderer) const = 0;
    virtual bool UsesFixedPreferredHeightInRows() const;
    virtual bool IsHoverable() const;
    virtual bool IsVerticalSpring() const;
    virtual void ResolveLayoutState(const WidgetHost& renderer, const RenderRect& rect);
    virtual void Draw(WidgetHost& renderer, const struct WidgetLayout& widget, const MetricSource& metrics) const;
    virtual void FinalizeLayoutGroup(WidgetHost& renderer, const std::vector<struct WidgetLayout*>& widgets);
    virtual void BuildEditGuides(WidgetHost& renderer, const struct WidgetLayout& widget) const;
    virtual void BuildStaticAnchors(WidgetHost& renderer, const struct WidgetLayout& widget) const;
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
std::unique_ptr<Widget> CreateCardChromeWidget(const LayoutCardConfig& card);
