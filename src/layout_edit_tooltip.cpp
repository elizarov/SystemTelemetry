#include "layout_edit_tooltip.h"

#include <cmath>
#include <iomanip>
#include <sstream>

std::string FormatLayoutEditTooltipValue(double value, LayoutEditTooltipValueFormat format) {
    if (format == LayoutEditTooltipValueFormat::FontSpec) {
        return {};
    }
    if (format == LayoutEditTooltipValueFormat::Integer) {
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

std::string FormatLayoutEditTooltipValue(const UiFontConfig& value) {
    return value.face + "," + std::to_string(value.size) + "," + std::to_string(value.weight);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, double value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " +
           FormatLayoutEditTooltipValue(value, descriptor.valueFormat);
}

std::string BuildLayoutEditTooltipLine(const LayoutEditTooltipDescriptor& descriptor, const UiFontConfig& value) {
    return "[" + descriptor.sectionName + "] " + descriptor.memberName + " = " + FormatLayoutEditTooltipValue(value);
}
