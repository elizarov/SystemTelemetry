#pragma once

#include <memory>
#include <string>

#include "telemetry/gpu/gpu_vendor.h"
#include "util/trace.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateIntelGpuTelemetryProvider(Trace& trace, std::string adapterName);
