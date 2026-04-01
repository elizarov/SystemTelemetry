#pragma once

#include <memory>
#include <optional>
#include <string>

struct GpuVendorTelemetrySample {
    std::optional<std::wstring> name;
    std::optional<double> temperatureC;
    std::optional<double> coreClockMhz;
    std::optional<double> fanRpm;
    std::wstring providerName = L"None";
    std::wstring diagnostics;
    bool available = false;
};

class GpuVendorTelemetryProvider {
public:
    virtual ~GpuVendorTelemetryProvider() = default;
    virtual bool Initialize() = 0;
    virtual GpuVendorTelemetrySample Sample() = 0;
};

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider();
