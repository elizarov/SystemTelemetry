#include "config/color_format.h"

#include <cstdio>

std::string FormatRgbaColorText(unsigned int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%08X", value);
    return buffer;
}
