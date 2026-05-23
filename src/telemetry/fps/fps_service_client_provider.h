#pragma once

#include <memory>
#include <optional>

#include "telemetry/fps_provider.h"
#include "telemetry/gpu/gpu_vendor_selection.h"

std::unique_ptr<FpsTelemetryProvider> CreatePresentedFpsProvider(
    Trace& trace,
    const std::optional<GpuAdapterInfo>& adapter = std::nullopt
);
