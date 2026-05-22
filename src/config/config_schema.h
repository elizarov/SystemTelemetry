#pragma once

#include <cstdint>

namespace configschema {

enum class ValueFormat : std::uint8_t {
    String,
    Integer,
    FloatingPoint,
    ColorHex,
    FontSpec,
};

}  // namespace configschema
