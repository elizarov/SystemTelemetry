#include "util/numeric_format.h"

#include <cstddef>

#include "util/text_format.h"

std::string FormatDoubleGeneral(double value, int precision) {
    const std::string format = FormatText("%%.%dg", precision);
    return FormatText(format.c_str(), value);
}

std::string FormatDoubleFixed(double value, int precision) {
    const std::string format = FormatText("%%.%df", precision);
    return FormatText(format.c_str(), value);
}

std::string FormatDoubleFixedTrimmed(double value, int precision) {
    std::string text = FormatDoubleFixed(value, precision);
    if (const std::size_t dot = text.find('.'); dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}
