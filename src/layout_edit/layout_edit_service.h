#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_controller.h"

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditHost::LayoutTarget& target);
const LayoutNodeConfig* FindEditableWidgetNode(const AppConfig& config, const LayoutEditWidgetIdentity& widget);
const LayoutNodeConfig* FindMetricListNode(const AppConfig& config, const LayoutMetricListOrderEditKey& key);
std::vector<std::string> ParseMetricListMetricRefs(std::string_view parameter);
std::vector<std::string> AvailableMetricListMetricIds(const AppConfig& config);

std::vector<int> SeedGuideWeights(const LayoutEditGuide& guide, const LayoutNodeConfig* node);

bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights);
bool ApplyMetricListOrder(
    AppConfig& config, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs);
bool ApplyContainerChildOrder(
    AppConfig& config, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex);
bool AppendMetricListRow(AppConfig& config, const LayoutEditWidgetIdentity& widget, std::string_view metricRef);

std::optional<int> EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const LayoutEditWidgetIdentity& widget,
    LayoutGuideAxis axis);
