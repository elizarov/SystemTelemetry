#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/config_schema.h"
#include "widget/layout_edit_types.h"

enum class LayoutEditEditorKind {
    Summary,
    Numeric,
    Font,
    GlobalFontFamily,
    Color,
    Weights,
    Metric,
    MetricListOrder,
    DateTimeFormat,
};

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

const LayoutNodeFieldEditDescriptor* FindLayoutNodeFieldEditDescriptor(const LayoutNodeFieldEditKey& key);
std::optional<LayoutNodeFieldEditKey> LayoutNodeFieldEditKeyForWidgetParameter(
    std::string editCardId, std::vector<size_t> nodePath, WidgetClass widgetClass);
std::wstring LayoutNodeFieldEditTitle(const LayoutNodeFieldEditKey& key);
std::wstring LayoutNodeFieldEditHint(const LayoutNodeFieldEditKey& key);
std::wstring LayoutNodeFieldEditMenuSubject(const LayoutNodeFieldEditKey& key);
std::string LayoutNodeFieldEditTraceLabel(const LayoutNodeFieldEditKey& key, std::string_view sectionName);
