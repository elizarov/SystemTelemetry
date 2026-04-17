#pragma once

#include <optional>
#include <vector>

#include "config.h"
#include "dashboard_renderer.h"
#include "layout_edit_controller.h"

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditHost::LayoutTarget& target);
const LayoutNodeConfig* FindEditableWidgetNode(const AppConfig& config, const LayoutEditWidgetIdentity& widget);

std::vector<int> SeedGuideWeights(const LayoutEditGuide& guide, const LayoutNodeConfig* node);

bool ApplyGuideWeights(AppConfig& config, const LayoutEditHost::LayoutTarget& target, const std::vector<int>& weights);
bool ApplyMetricListOrder(
    AppConfig& config, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs);

std::optional<int> EvaluateWidgetExtentForGuideWeights(DashboardRenderer& renderer,
    const LayoutEditHost::LayoutTarget& target,
    const std::vector<int>& weights,
    const LayoutEditWidgetIdentity& widget,
    LayoutGuideAxis axis);
