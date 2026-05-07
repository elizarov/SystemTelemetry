#include "layout_edit/layout_edit_target_descriptor.h"

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
        "Metric List",
        "Choose the metric for each row, move rows up or down, remove rows, or add a new row.",
        "metric_list_order"},
    {WidgetClass::ClockTime,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::DateTimeFormat,
        configschema::ValueFormat::String,
        "clock_time",
        "layout_edit.clock_time_format",
        "Time Format",
        "Choose a time format. Changes preview live.",
        "date_time_format"},
    {WidgetClass::ClockDate,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::DateTimeFormat,
        configschema::ValueFormat::String,
        "clock_date",
        "layout_edit.clock_date_format",
        "Date Format",
        "Choose a date format. Changes preview live.",
        "date_time_format"},
}};

}  // namespace

const LayoutNodeFieldEditDescriptor* FindLayoutNodeFieldEditDescriptor(const LayoutNodeFieldEditKey& key) {
    for (const LayoutNodeFieldEditDescriptor& descriptor : kNodeFieldDescriptors) {
        if (descriptor.widgetClass == key.widgetClass && descriptor.field == key.field) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::optional<LayoutNodeFieldEditKey> LayoutNodeFieldEditKeyForWidgetParameter(
    std::string editCardId, std::vector<size_t> nodePath, WidgetClass widgetClass) {
    LayoutNodeFieldEditKey key{std::move(editCardId), std::move(nodePath), widgetClass, LayoutNodeField::Parameter};
    return FindLayoutNodeFieldEditDescriptor(key) != nullptr ? std::optional<LayoutNodeFieldEditKey>(std::move(key))
                                                             : std::nullopt;
}

std::string LayoutNodeFieldEditTitle(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? std::string(descriptor->title) : std::string{};
}

std::string LayoutNodeFieldEditHint(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? std::string(descriptor->hint) : std::string{};
}

std::string LayoutNodeFieldEditMenuSubject(const LayoutNodeFieldEditKey& key) {
    return LayoutNodeFieldEditTitle(key);
}

std::string LayoutNodeFieldEditTraceLabel(const LayoutNodeFieldEditKey& key, std::string_view sectionName) {
    const std::string prefix(sectionName);
    return prefix.empty() ? std::string(EnumToString(key.widgetClass))
                          : prefix + "." + std::string(EnumToString(key.widgetClass));
}
