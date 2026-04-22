#pragma once

#include <memory>

#include "telemetry/gpu/gpu_vendor.h"
#include "util/trace.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(tracing::Trace* trace);
