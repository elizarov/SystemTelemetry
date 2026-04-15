#pragma once

#include <optional>
#include <string>

enum class ScalarMetricUnit {
    None,
    Celsius,
    Gigahertz,
    Megahertz,
    Rpm,
};

struct ScalarMetric {
    std::optional<double> value;
    ScalarMetricUnit unit = ScalarMetricUnit::None;
};

struct NamedScalarMetric {
    std::string name;
    ScalarMetric metric;
};
