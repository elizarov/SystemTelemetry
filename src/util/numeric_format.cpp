#include "util/numeric_format.h"

#include <cstddef>
#include <cstdio>

std::string FormatDoubleGeneral(double value, int precision) {
    char format[16];
    sprintf_s(format, "%%.%dg", precision);
    char buffer[64];
    sprintf_s(buffer, format, value);
    return buffer;
}

std::string FormatDoubleFixed(double value, int precision) {
    char format[16];
    sprintf_s(format, "%%.%df", precision);
    char buffer[64];
    sprintf_s(buffer, format, value);
    return buffer;
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
