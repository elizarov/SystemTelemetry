#pragma once

#include <memory>
#include <vector>

#include "telemetry/gpu/gpu_vendor_selection.h"

class Trace;

class GpuDiscoveryHdi {
public:
    virtual ~GpuDiscoveryHdi() = default;

    virtual std::vector<GpuAdapterInfo> EnumerateAdapters() = 0;
};

std::unique_ptr<GpuDiscoveryHdi> CreateProductionGpuDiscoveryHdi(Trace& trace);
