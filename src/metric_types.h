#pragma once

#include "enum_string.h"

#include <optional>
#include <string>

#define SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS(X) \
    X(None, "") \
    X(Celsius, "C") \
    X(Gigahertz, "GHz") \
    X(Megahertz, "MHz") \
    X(Rpm, "RPM")

enum class ScalarMetricUnit {
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) name,
    SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
};

template <> struct EnumStringTraits<ScalarMetricUnit> {
    static constexpr auto values = std::to_array<ScalarMetricUnit>({
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) ScalarMetricUnit::name,
        SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
    });

    static constexpr auto names = std::to_array<std::string_view>({
#define SYSTEM_TELEMETRY_ENUM_ITEM(name, text) text,
        SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS(SYSTEM_TELEMETRY_ENUM_ITEM)
#undef SYSTEM_TELEMETRY_ENUM_ITEM
    });

    static_assert(enum_string_detail::ValidateCanonicalMappings(values, names));
};

#undef SYSTEM_TELEMETRY_SCALAR_METRIC_UNIT_ITEMS

struct ScalarMetric {
    std::optional<double> value;
    ScalarMetricUnit unit = ScalarMetricUnit::None;
};

struct NamedScalarMetric {
    std::string name;
    ScalarMetric metric;
};
