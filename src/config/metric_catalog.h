#pragma once

#include <optional>
#include <string_view>

#include "config/metric_display_style.h"

using ConfigMetricStyleResolver = std::optional<MetricDisplayStyle> (*)(std::string_view metricRef);

struct ConfigMetricCatalog {
    ConfigMetricStyleResolver resolveMetricDisplayStyle = nullptr;

    std::optional<MetricDisplayStyle> FindMetricDisplayStyle(std::string_view metricRef) const;
};

struct ConfigParseContext {
    ConfigMetricCatalog metricCatalog;
};
