#include "telemetry/gpu/gpu_vendor.h"

#include "telemetry/gpu/amd/gpu_amd_adl.h"
#include "util/trace.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(Trace& trace) {
    return CreateAmdGpuTelemetryProvider(trace);
}
