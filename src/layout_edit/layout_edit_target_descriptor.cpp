#include "layout_edit/layout_edit_target_descriptor.h"

#include <array>
#include <utility>

#include "util/localization_catalog.h"
#include "util/text_format.h"

namespace {

constexpr std::array<LayoutNodeFieldEditDescriptor, 3> kNodeFieldDescriptors{{
    {WidgetClass::MetricList,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::MetricListOrder,
        configschema::ValueFormat::String,
        "metric_list",
        "layout_edit.metric_list_reorder",
        RES_STR("layout_edit.metric_list_reorder"),
        RES_STR("layout_edit.node_field.metric_list.title"),
        RES_STR("layout_edit.node_field.metric_list.hint"),
        "metric_list_order"},
    {WidgetClass::ClockTime,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::DateTimeFormat,
        configschema::ValueFormat::String,
        "clock_time",
        "layout_edit.clock_time_format",
        RES_STR("layout_edit.clock_time_format"),
        RES_STR("layout_edit.node_field.clock_time.title"),
        RES_STR("layout_edit.node_field.clock_time.hint"),
        "date_time_format"},
    {WidgetClass::ClockDate,
        LayoutNodeField::Parameter,
        LayoutEditEditorKind::DateTimeFormat,
        configschema::ValueFormat::String,
        "clock_date",
        "layout_edit.clock_date_format",
        RES_STR("layout_edit.clock_date_format"),
        RES_STR("layout_edit.node_field.clock_date.title"),
        RES_STR("layout_edit.node_field.clock_date.hint"),
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

std::string_view LayoutNodeFieldEditTitle(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? FindLocalizedText(descriptor->titleKey) : std::string_view{};
}

std::string_view LayoutNodeFieldEditHint(const LayoutNodeFieldEditKey& key) {
    const LayoutNodeFieldEditDescriptor* descriptor = FindLayoutNodeFieldEditDescriptor(key);
    return descriptor != nullptr ? FindLocalizedText(descriptor->hintKey) : std::string_view{};
}

std::string_view LayoutNodeFieldEditMenuSubject(const LayoutNodeFieldEditKey& key) {
    return LayoutNodeFieldEditTitle(key);
}

std::string LayoutNodeFieldEditTraceLabel(const LayoutNodeFieldEditKey& key, std::string_view sectionName) {
    const std::string prefix(sectionName);
    return prefix.empty() ? std::string(EnumToString(key.widgetClass))
                          : FormatText("%s.%s", prefix.c_str(), EnumToString(key.widgetClass));
}
