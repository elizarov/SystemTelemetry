#pragma once

#include <memory>
#include <optional>
#include <string>

namespace tracing {
class Trace;
}

struct GpuVendorTelemetrySample {
    std::optional<std::string> name;
    std::optional<double> temperatureC;
    std::optional<double> coreClockMhz;
    std::optional<double> fanRpm;
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

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(tracing::Trace* trace);
