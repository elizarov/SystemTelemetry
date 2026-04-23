#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/config.h"
#include "config/widget_class.h"
#include "widget/render_types.h"

class MetricSource;
class WidgetRenderer;

class DashboardWidget {
public:
    virtual ~DashboardWidget() = default;

    virtual DashboardWidgetClass Class() const = 0;
    virtual std::unique_ptr<DashboardWidget> Clone() const = 0;
    virtual void Initialize(const LayoutNodeConfig& node) = 0;
    virtual int PreferredHeight(const WidgetRenderer& renderer) const = 0;
    virtual bool UsesFixedPreferredHeightInRows() const;
    virtual bool IsHoverable() const;
    virtual bool IsVerticalSpring() const;
    virtual void ResolveLayoutState(const WidgetRenderer& renderer, const RenderRect& rect);
    virtual void Draw(
        WidgetRenderer& renderer, const struct DashboardWidgetLayout& widget, const MetricSource& metrics) const;
    virtual void FinalizeLayoutGroup(
        WidgetRenderer& renderer, const std::vector<struct DashboardWidgetLayout*>& widgets);
    virtual void BuildEditGuides(WidgetRenderer& renderer, const struct DashboardWidgetLayout& widget) const;
    virtual void BuildStaticAnchors(WidgetRenderer& renderer, const struct DashboardWidgetLayout& widget) const;
};

struct DashboardWidgetLayout {
    RenderRect rect{};
    std::string cardId;
    std::string editCardId;
    std::vector<size_t> nodePath;
    std::unique_ptr<DashboardWidget> widget;
};

std::unique_ptr<DashboardWidget> CreateDashboardWidget(DashboardWidgetClass widgetClass);
std::unique_ptr<DashboardWidget> CreateDashboardWidget(std::string_view name);
