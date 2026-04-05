#pragma once

#include <optional>
#include <string>

struct ScalarMetric {
    std::optional<double> value;
    std::string unit;
};

struct NamedScalarMetric {
    std::string name;
    ScalarMetric metric;
};
