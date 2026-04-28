#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "layout_edit/layout_edit_target_descriptor.h"
#include "layout_model/layout_edit_layout_target.h"

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditLayoutTarget& target);
const LayoutNodeConfig* FindEditableWidgetNode(const AppConfig& config, const LayoutEditWidgetIdentity& widget);
const LayoutNodeConfig* FindLayoutNodeFieldNode(const AppConfig& config, const LayoutNodeFieldEditKey& key);
std::string ReadLayoutNodeFieldValue(const LayoutNodeConfig& node, LayoutNodeField field);
std::vector<std::string> ParseMetricListMetricRefs(std::string_view parameter);
std::vector<std::string> AvailableMetricListMetricIds(const AppConfig& config, const ConfigMetricCatalog& catalog);

std::vector<int> SeedGuideWeights(const LayoutEditGuide& guide, const LayoutNodeConfig* node);

bool ApplyLayoutNodeFieldValue(AppConfig& config, const LayoutNodeFieldEditKey& key, std::string_view value);
bool ApplyLayoutEditValue(AppConfig& config, const LayoutEditFocusKey& key, const LayoutEditValue& value);
bool ApplyMetricListOrder(
    AppConfig& config, const LayoutEditWidgetIdentity& widget, const std::vector<std::string>& metricRefs);
bool ApplyContainerChildOrder(
    AppConfig& config, const LayoutContainerChildOrderEditKey& key, int fromIndex, int toIndex);
bool AppendMetricListRow(AppConfig& config, const LayoutEditWidgetIdentity& widget, std::string_view metricRef);
