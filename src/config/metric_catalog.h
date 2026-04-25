#pragma once

#include <optional>
#include <string_view>

#include "config/metric_display_style.h"

using ConfigMetricStyleResolver = std::optional<MetricDisplayStyle> (*)(std::string_view metricRef);
using ConfigMetricAvailabilityResolver = bool (*)(std::string_view metricRef);

struct ConfigMetricCatalog {
    ConfigMetricStyleResolver resolveMetricDisplayStyle = nullptr;
    ConfigMetricAvailabilityResolver isGenerallyAvailableMetric = nullptr;

    std::optional<MetricDisplayStyle> FindMetricDisplayStyle(std::string_view metricRef) const;
    bool IsGenerallyAvailableMetric(std::string_view metricRef) const;
};

struct ConfigParseContext {
    ConfigMetricCatalog metricCatalog;
};
