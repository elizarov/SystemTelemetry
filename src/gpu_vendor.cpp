#include "gpu_vendor.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider();

std::unique_ptr<GpuVendorTelemetryProvider> CreateGpuVendorTelemetryProvider() {
    return CreateAmdGpuTelemetryProvider();
}
