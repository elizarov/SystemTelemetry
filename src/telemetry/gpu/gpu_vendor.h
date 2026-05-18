#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "telemetry/gpu/gpu_vendor_selection.h"
#include "util/trace.h"

struct GpuVendorTelemetrySample {
    std::optional<std::string> name;
    std::optional<double> loadPercent;
    std::optional<double> temperatureC;
    std::optional<double> coreClockMhz;
    std::optional<double> fanRpm;
    std::optional<double> fps;
    std::string fpsAppName;
    bool fpsPermissionRequired = false;
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

struct GpuAdapterSelection {
    std::optional<GpuAdapterInfo> selectedAdapter;
    std::vector<GpuAdapterCandidate> candidates;
};

GpuAdapterSelection ResolveGpuAdapterSelection(Trace& trace, std::string_view preferredAdapterName);
std::optional<GpuAdapterInfo> ExtractPrimaryGpuAdapterInfo(Trace& trace);
std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(
    Trace& trace, const std::optional<GpuAdapterInfo>& adapter);
