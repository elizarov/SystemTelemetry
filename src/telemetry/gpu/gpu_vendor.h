#pragma once

#include <memory>
#include <optional>
#include <string>

#include "util/trace.h"

struct GpuVendorTelemetrySample {
    std::optional<std::string> name;
    std::optional<double> loadPercent;
    std::optional<double> temperatureC;
    std::optional<double> coreClockMhz;
    std::optional<double> fanRpm;
    std::optional<double> usedVramGb;
    std::optional<double> totalVramGb;
    std::string providerName = "None";
    std::string diagnostics;
    bool available = false;
};

class GpuVendorTelemetryProvider {
public:
    virtual ~GpuVendorTelemetryProvider() = default;
    virtual bool Initialize() = 0;
    virtual GpuVendorTelemetrySample Sample() = 0;
};

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(Trace& trace);
