#include "telemetry/gpu/gpu_vendor.h"

#include "util/trace.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(tracing::Trace* trace);

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(tracing::Trace* trace) {
    return CreateAmdGpuTelemetryProvider(trace);
}
