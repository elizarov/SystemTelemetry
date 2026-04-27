#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/telemetry_settings.h"
#include "telemetry/metric_types.h"
#include "util/trace.h"

struct BoardVendorTelemetrySample {
    std::string boardManufacturer;
    std::string boardProduct;
    std::string driverLibrary;
    std::vector<std::string> requestedFanNames;
    std::vector<std::string> requestedTemperatureNames;
    std::vector<std::string> availableFanNames;
    std::vector<std::string> availableTemperatureNames;
    std::vector<NamedScalarMetric> fans;
    std::vector<NamedScalarMetric> temperatures;
    std::string providerName = "None";
    std::string diagnostics;
    bool available = false;
};

class BoardVendorTelemetryProvider {
public:
    virtual ~BoardVendorTelemetryProvider() = default;
    virtual bool Initialize(const BoardTelemetrySettings& settings) = 0;
    virtual BoardVendorTelemetrySample Sample() = 0;
};

std::unique_ptr<BoardVendorTelemetryProvider> CreateBoardVendorTelemetryProvider(Trace& trace);
