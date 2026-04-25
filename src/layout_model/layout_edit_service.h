#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "layout_model/layout_edit_layout_target.h"

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditLayoutTarget& target);
const LayoutNodeConfig* FindEditableWidgetNode(const AppConfig& config, const LayoutEditWidgetIdentity& widget);
const LayoutNodeConfig* FindMetricListNode(const AppConfig& config, const LayoutMetricListOrderEditKey& key);
std::vector<std::string> ParseMetricListMetricRefs(std::string_view parameter);
std::vector<std::string> AvailableMetricListMetricIds(const AppConfig& config);

std::vector<int> SeedGuideWeights(const LayoutEditGuide& guide, const LayoutNodeConfig* node);

bool ApplyGuideWeights(AppConfig& config, const LayoutEditLayoutTarget& target, const std::vector<int>& weights);
bool ApplyMetricListOrder(
    AppConfig& config, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs);
bool ApplyContainerChildOrder(
    AppConfig& config, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex);
bool AppendMetricListRow(AppConfig& config, const LayoutEditWidgetIdentity& widget, std::string_view metricRef);
