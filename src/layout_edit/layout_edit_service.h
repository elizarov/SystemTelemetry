#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "layout_model/layout_edit_layout_target.h"

struct LayoutNodeFieldEditDescriptor {
    WidgetClass widgetClass = WidgetClass::Unknown;
    LayoutNodeField field = LayoutNodeField::Parameter;
    LayoutEditEditorKind editorKind = LayoutEditEditorKind::Summary;
    configschema::ValueFormat valueFormat = configschema::ValueFormat::String;
    std::string_view label;
    std::string_view descriptionKey;
    std::wstring_view title;
    std::wstring_view hint;
    std::string_view traceName;
};

const LayoutNodeConfig* FindGuideNode(const AppConfig& config, const LayoutEditLayoutTarget& target);
const LayoutNodeConfig* FindEditableWidgetNode(const AppConfig& config, const LayoutEditWidgetIdentity& widget);
const LayoutNodeConfig* FindLayoutNodeFieldNode(const AppConfig& config, const LayoutNodeFieldEditKey& key);
const LayoutNodeFieldEditDescriptor* FindLayoutNodeFieldEditDescriptor(const LayoutNodeFieldEditKey& key);
std::optional<LayoutNodeFieldEditKey> LayoutNodeFieldEditKeyForWidgetParameter(
    std::string editCardId, std::vector<size_t> nodePath, WidgetClass widgetClass);
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
