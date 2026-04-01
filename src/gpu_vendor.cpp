#include "gpu_vendor.h"

#include "trace.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(tracing::Trace* trace);

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(tracing::Trace* trace) {
    return CreateAmdGpuTelemetryProvider(trace);
}
