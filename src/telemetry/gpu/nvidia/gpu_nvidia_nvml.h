#pragma once

#include <memory>
#include <optional>

#include "telemetry/gpu/gpu_vendor.h"
#include "util/trace.h"

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(
    Trace& trace, std::optional<GpuAdapterInfo> adapter);
