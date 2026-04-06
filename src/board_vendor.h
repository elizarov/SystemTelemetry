#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "metric_types.h"

namespace tracing {
class Trace;
}

struct BoardVendorTelemetrySample {
    std::string boardManufacturer;
    std::string boardProduct;
    std::string driverLibrary;
    std::vector<std::string> requestedFanNames;
    std::vector<std::string> requestedTemperatureNames;
    std::vector<NamedScalarMetric> fans;
    std::vector<NamedScalarMetric> temperatures;
    std::string providerName = "None";
    std::string diagnostics;
    bool available = false;
};

class BoardVendorTelemetryProvider {
public:
    virtual ~BoardVendorTelemetryProvider() = default;
    virtual bool Initialize(const AppConfig& config) = 0;
    virtual BoardVendorTelemetrySample Sample() = 0;
};

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(tracing::Trace* trace);
