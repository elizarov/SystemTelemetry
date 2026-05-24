#pragma once

#include <memory>

class BoardDiscoveryHdi;
class GpuDiscoveryHdi;
class GpuPerformanceHdi;
class MsiCenterHdi;
class NvidiaNvapiHdi;
class NvidiaNvmlHdi;
class Trace;

class HdiFactory {
public:
    virtual ~HdiFactory() = default;

    virtual std::unique_ptr<GpuDiscoveryHdi> CreateGpuDiscoveryHdi(Trace& trace) = 0;
    virtual std::unique_ptr<GpuPerformanceHdi> CreateGpuPerformanceHdi(Trace& trace) = 0;
    virtual std::unique_ptr<BoardDiscoveryHdi> CreateBoardDiscoveryHdi() = 0;
    virtual std::unique_ptr<NvidiaNvmlHdi> CreateNvidiaNvmlHdi(Trace& trace) = 0;
    virtual std::unique_ptr<NvidiaNvapiHdi> CreateNvidiaNvapiHdi(Trace& trace) = 0;
    virtual std::unique_ptr<MsiCenterHdi> CreateMsiCenterHdi(Trace& trace) = 0;
};

struct HardwareDependencyInjection {
    // Borrowed by TelemetryRuntime. The factory and any mock state it returns must outlive the runtime.
    HdiFactory* factory = nullptr;
};

HdiFactory& ResolveHdiFactory(const HardwareDependencyInjection* injection);
