#include "config/config_primitives.h"

#include "config/color_format.h"

ColorConfig ColorConfig::FromRgba(unsigned int value) {
    return ColorConfig{static_cast<std::uint32_t>(value), FormatRgbaColorText(value)};
}

unsigned int ColorConfig::ToRgb() const {
    return (rgba >> 8) & 0xFFFFFFu;
}

unsigned int ColorConfig::ToRgba() const {
    return rgba;
}

std::uint8_t ColorConfig::Alpha() const {
    return static_cast<std::uint8_t>(rgba & 0xFFu);
}
