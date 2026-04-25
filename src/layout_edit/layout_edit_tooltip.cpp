#include "layout_edit/layout_edit_tooltip.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "layout_edit/layout_edit_service.h"

std::string FormatLayoutEditTooltipValue(double value, configschema::ValueFormat format) {
    if (format == configschema::ValueFormat::String || format == configschema::ValueFormat::FontSpec ||
        format == configschema::ValueFormat::ColorHex) {
        return {};
    }
    if (format == configschema::ValueFormat::Integer) {
        return std::to_string(static_cast<int>(std::lround(value)));
    }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::string text = stream.str();
    const size_t dot = text.find('.');
    if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string FormatLayoutEditTooltipValue(unsigned int value) {
    std::ostringstream stream;
    stream << '#' << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << value;
    return stream.str();
}

std::string FormatLayoutEditTooltipValue(const UiFontConfig& value) {
    return value.face + "," + std::to_string(value.size) + "," + std::to_string(value.weight);
}

std::string FormatLayoutEditTooltipValue(std::string_view value) {
    return std::string(value);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " +
           FormatLayoutEditTooltipValue(value, descriptor.valueFormat);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, unsigned int value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " + FormatLayoutEditTooltipValue(value);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " + FormatLayoutEditTooltipValue(value);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, std::string_view value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " + FormatLayoutEditTooltipValue(value);
}

std::optional<std::string> BuildMetricListOrderTooltipLine(
    const AppConfig& config, const LayoutMetricListOrderEditKey& key, int rowIndex) {
    const LayoutNodeConfig* node = FindMetricListNode(config, key);
    if (node == nullptr || node->name != "metric_list" || rowIndex < 0) {
        return std::nullopt;
    }

    const std::vector<std::string> metricRefs = ParseMetricListMetricRefs(node->parameter);
    if (rowIndex >= static_cast<int>(metricRefs.size())) {
        return std::nullopt;
    }

    const std::string sectionName = key.editCardId.empty() && !config.display.layout.empty()
                                        ? "layout." + config.display.layout
                                    : key.editCardId.empty() ? "layout"
                                                             : "card." + key.editCardId;
    const std::string memberName = key.editCardId.empty() ? "cards" : "layout";
    return "[" + sectionName + "] " + memberName + " = metric_list(" + metricRefs[static_cast<size_t>(rowIndex)] + ")";
}

std::optional<std::string> BuildMetricListAddRowTooltipLine(
    const AppConfig& config, const LayoutMetricListOrderEditKey& key) {
    const LayoutNodeConfig* node = FindMetricListNode(config, key);
    if (node == nullptr || node->name != "metric_list") {
        return std::nullopt;
    }

    const std::string sectionName = key.editCardId.empty() && !config.display.layout.empty()
                                        ? "layout." + config.display.layout
                                    : key.editCardId.empty() ? "layout"
                                                             : "card." + key.editCardId;
    const std::string memberName = key.editCardId.empty() ? "cards" : "layout";
    return "[" + sectionName + "] " + memberName + " = metric_list(...)";
}
