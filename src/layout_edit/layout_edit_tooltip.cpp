#include "layout_edit/layout_edit_tooltip.h"

#include <cmath>

#include "config/color_format.h"
#include "layout_edit/layout_edit_service.h"
#include "util/numeric_format.h"
#include "util/text_format.h"

std::string FormatLayoutEditTooltipValue(double value, configschema::ValueFormat format) {
    if (format == configschema::ValueFormat::String || format == configschema::ValueFormat::FontSpec ||
        format == configschema::ValueFormat::ColorHex) {
        return {};
    }
    if (format == configschema::ValueFormat::Integer) {
        return FormatText("%d", static_cast<int>(std::lround(value)));
    }

    return FormatDoubleFixedTrimmed(value, 2);
}

std::string FormatLayoutEditTooltipValue(unsigned int value) {
    return FormatRgbaColorText(value);
}

std::string FormatLayoutEditTooltipValue(const UiFontConfig& value) {
    return FormatText("%s,%d,%d", value.face.c_str(), value.size, value.weight);
}

std::string FormatLayoutEditTooltipValue(std::string_view value) {
    return std::string(value);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value) {
    return FormatText("[%s] %s = %s",
        descriptor.sectionName.c_str(),
        descriptor.memberName.c_str(),
        FormatLayoutEditTooltipValue(value, descriptor.valueFormat).c_str());
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, unsigned int value) {
    return FormatText("[%s] %s = %s",
        descriptor.sectionName.c_str(),
        descriptor.memberName.c_str(),
        FormatLayoutEditTooltipValue(value).c_str());
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value) {
    return FormatText("[%s] %s = %s",
        descriptor.sectionName.c_str(),
        descriptor.memberName.c_str(),
        FormatLayoutEditTooltipValue(value).c_str());
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, std::string_view value) {
    return FormatText("[%s] %s = %s",
        descriptor.sectionName.c_str(),
        descriptor.memberName.c_str(),
        FormatLayoutEditTooltipValue(value).c_str());
}

std::optional<std::string> BuildMetricListOrderTooltipLine(
    const AppConfig& config, const LayoutNodeFieldEditKey& key, int rowIndex) {
    const LayoutNodeConfig* node = FindLayoutNodeFieldNode(config, key);
    if (node == nullptr || node->name != "metric_list" || rowIndex < 0) {
        return std::nullopt;
    }

    const std::vector<std::string> metricRefs = ParseMetricListMetricRefs(node->parameter);
    if (rowIndex >= static_cast<int>(metricRefs.size())) {
        return std::nullopt;
    }

    const std::string sectionName = key.editCardId.empty() && !config.display.layout.empty() ?
        FormatText("layout.%s", config.display.layout.c_str()) :
        key.editCardId.empty() ?
        "layout" :
        FormatText("card.%s", key.editCardId.c_str());
    const std::string memberName = key.editCardId.empty() ? "cards" : "layout";
    return FormatText("[%s] %s = metric_list(%s)",
        sectionName.c_str(),
        memberName.c_str(),
        metricRefs[static_cast<size_t>(rowIndex)].c_str());
}

std::optional<std::string> BuildMetricListAddRowTooltipLine(
    const AppConfig& config, const LayoutNodeFieldEditKey& key) {
    const LayoutNodeConfig* node = FindLayoutNodeFieldNode(config, key);
    if (node == nullptr || node->name != "metric_list") {
        return std::nullopt;
    }

    const std::string sectionName = key.editCardId.empty() && !config.display.layout.empty() ?
        FormatText("layout.%s", config.display.layout.c_str()) :
        key.editCardId.empty() ?
        "layout" :
        FormatText("card.%s", key.editCardId.c_str());
    const std::string memberName = key.editCardId.empty() ? "cards" : "layout";
    return FormatText("[%s] %s = metric_list()", sectionName.c_str(), memberName.c_str());
}

std::optional<std::string> BuildContainerChildOrderTooltipLine(
    const AppConfig& config, const LayoutContainerChildOrderEditKey& key) {
    const LayoutNodeConfig* node = FindGuideNode(config, LayoutEditLayoutTarget{key.editCardId, key.nodePath});
    if (node == nullptr || (node->name != "rows" && node->name != "columns") || node->children.size() < 2) {
        return std::nullopt;
    }

    const std::string sectionName = key.editCardId.empty() && !config.display.layout.empty() ?
        FormatText("layout.%s", config.display.layout.c_str()) :
        key.editCardId.empty() ?
        "layout" :
        FormatText("card.%s", key.editCardId.c_str());
    const std::string memberName = key.editCardId.empty() ? "cards" : "layout";
    std::string text = FormatText("[%s] %s = %s(", sectionName.c_str(), memberName.c_str(), node->name.c_str());
    for (size_t i = 0; i < node->children.size(); ++i) {
        if (i > 0) {
            AppendFormat(text, ", ");
        }
        AppendFormat(text, "%s", node->children[i].name.empty() ? "unknown" : node->children[i].name.c_str());
    }
    AppendFormat(text, ")");
    return text;
}
