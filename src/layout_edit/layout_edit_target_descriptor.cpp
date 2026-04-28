#include "layout_edit/layout_edit_target_descriptor.h"

#include <algorithm>
#include <array>
#include <utility>

namespace {

constexpr std::array<LayoutNodeFieldEditDescriptor, 3> kNodeFieldDescriptors{{
    {WidgetClass::MetricList,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::MetricListOrder,
        configschema::ValueFormat::String,
        "metric_list",
        "layout_edit.metric_list_reorder",
        L"Metric List",
        L"Choose the metric for each row, move rows up or down, remove rows, or add a new row.",
        "metric_list_order"},
    {WidgetClass::ClockTime,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::DateTimeFormat,
        configschema::ValueFormat::String,
        "clock_time",
        "layout_edit.clock_time_format",
        L"Time Format",
        L"Choose a time format. Changes preview live.",
        "date_time_format"},
    {WidgetClass::ClockDate,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::DateTimeFormat,
        configschema::ValueFormat::String,
        "clock_date",
        "layout_edit.clock_date_format",
        L"Date Format",
        L"Choose a date format. Changes preview live.",
        "date_time_format"},
}};

}  // namespace

const LayoutNodeFieldEditDescriptor* FindLayoutNodeFieldEditDescriptor(const LayoutNodeFieldEditKey& key) {
    const auto it = std::find_if(kNodeFieldDescriptors.begin(),
        kNodeFieldDescriptors.end(),
        [&](const LayoutNodeFieldEditDescriptor& descriptor) {
            return descriptor.widgetClass == key.widgetClass && descriptor.field == key.field;
        });
    return it != kNodeFieldDescriptors.end() ? &(*it) : nullptr;
}

std::optional<LayoutNodeFieldEditKey> LayoutNodeFieldEditKeyForWidgetParameter(
    std::string editCardId, std::vector<size_t> nodePath, WidgetClass widgetClass) {
    LayoutNodeFieldEditKey key{std::move(editCardId), std::move(nodePath), widgetClass, LayoutNodeField::Parameter};
    return FindLayoutNodeFieldEditDescriptor(key) != nullptr ? std::optional<LayoutNodeFieldEditKey>(std::move(key))
                                                             : std::nullopt;
}

std::wstring LayoutNodeFieldEditTitle(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? std::wstring(descriptor->title) : L"";
}

std::wstring LayoutNodeFieldEditHint(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? std::wstring(descriptor->hint) : L"";
}

std::wstring LayoutNodeFieldEditMenuSubject(const LayoutNodeFieldEditKey& key) {
    return LayoutNodeFieldEditTitle(key);
}

std::string LayoutNodeFieldEditTraceLabel(const LayoutNodeFieldEditKey& key, std::string_view sectionName) {
    const std::string prefix(sectionName);
    return prefix.empty() ? std::string(EnumToString(key.widgetClass))
                          : prefix + "." + std::string(EnumToString(key.widgetClass));
}
