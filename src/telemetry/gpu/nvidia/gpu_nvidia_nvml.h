#pragma once

#include <memory>
#include <optional>

#include "telemetry/gpu/gpu_vendor.h"
#include "util/trace.h"

struct HardwareDependencyInjection;

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(
    Trace& trace, std::optional<GpuAdapterInfo> adapter, bool collectPresentedFps);
std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(Trace& trace,
    std::optional<GpuAdapterInfo> adapter,
    bool collectPresentedFps,
    const HardwareDependencyInjection* injection);
