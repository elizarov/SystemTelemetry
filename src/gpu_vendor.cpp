#include "gpu_vendor.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(std::ostream* traceStream);

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider(std::ostream* traceStream) {
    return CreateAmdGpuTelemetryProvider(traceStream);
}
