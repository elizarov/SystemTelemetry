#include "config/color_format.h"

#include "util/text_format.h"

std::string FormatRgbaColorText(unsigned int value) {
    return FormatText("#%08X", value);
}
