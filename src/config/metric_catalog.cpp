#include "config/metric_catalog.h"

std::optional<MetricDisplayStyle> ConfigMetricCatalog::FindMetricDisplayStyle(std::string_view metricRef) const {
    if (resolveMetricDisplayStyle == nullptr) {
        return std::nullopt;
    }
    return resolveMetricDisplayStyle(metricRef);
}
