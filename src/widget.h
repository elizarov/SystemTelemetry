#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

#include "config.h"

class DashboardMetricSource;
class DashboardRenderer;

enum class DashboardWidgetClass {
    Unknown,
    Text,
    Gauge,
    MetricList,
    Throughput,
    NetworkFooter,
    Spacer,
    VerticalSpring,
    DriveUsageList,
    ClockTime,
    ClockDate,
};

class DashboardWidget {
public:
    virtual ~DashboardWidget() = default;

    virtual DashboardWidgetClass Class() const = 0;
    virtual std::unique_ptr<DashboardWidget> Clone() const = 0;
    virtual void Initialize(const LayoutNodeConfig& node) = 0;
    virtual int PreferredHeight(const DashboardRenderer& renderer) const = 0;
    virtual bool UsesFixedPreferredHeightInRows() const;
    virtual bool IsHoverable() const;
    virtual bool IsVerticalSpring() const;
    virtual void Draw(DashboardRenderer& renderer,
        HDC hdc,
        const struct DashboardWidgetLayout& widget,
        const DashboardMetricSource& metrics) const;
    virtual void BuildEditGuides(DashboardRenderer& renderer, const struct DashboardWidgetLayout& widget) const;
};

struct DashboardWidgetLayout {
    RECT rect{};
    std::string cardId;
    std::string editCardId;
    std::vector<size_t> nodePath;
    std::unique_ptr<DashboardWidget> widget;
};

std::optional<DashboardWidgetClass> FindDashboardWidgetClass(std::string_view name);
std::string_view DashboardWidgetClassName(DashboardWidgetClass widgetClass);
std::unique_ptr<DashboardWidget> CreateDashboardWidget(DashboardWidgetClass widgetClass);
std::unique_ptr<DashboardWidget> CreateDashboardWidget(std::string_view name);
