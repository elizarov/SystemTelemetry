#include "telemetry/impl/hdi_default.h"

#include "telemetry/board/msi/impl/hdi_msi_center.h"
#include "telemetry/gpu/nvidia/impl/hdi_nvapi.h"
#include "telemetry/gpu/nvidia/impl/hdi_nvml.h"
#include "telemetry/impl/hdi.h"
#include "telemetry/impl/hdi_board_discovery.h"
#include "telemetry/impl/hdi_gpu_discovery.h"
#include "telemetry/impl/hdi_gpu_performance.h"

namespace {

class ProductionHdiFactory final : public HdiFactory {
public:
    std::unique_ptr<GpuDiscoveryHdi> CreateGpuDiscoveryHdi(Trace& trace) override {
        return CreateProductionGpuDiscoveryHdi(trace);
    }

    std::unique_ptr<GpuPerformanceHdi> CreateGpuPerformanceHdi(Trace& trace) override {
        return CreateProductionGpuPerformanceHdi(trace);
    }

    std::unique_ptr<BoardDiscoveryHdi> CreateBoardDiscoveryHdi() override {
        return CreateProductionBoardDiscoveryHdi();
    }

    std::unique_ptr<NvidiaNvmlHdi> CreateNvidiaNvmlHdi(Trace& trace) override {
        return CreateProductionNvidiaNvmlHdi(trace);
    }

    std::unique_ptr<NvidiaNvapiHdi> CreateNvidiaNvapiHdi(Trace& trace) override {
        return CreateProductionNvidiaNvapiHdi(trace);
    }

    std::unique_ptr<MsiCenterHdi> CreateMsiCenterHdi(Trace& trace) override {
        return CreateProductionMsiCenterHdi(trace);
    }
};

}  // namespace

HdiFactory& DefaultHdiFactory() {
    static ProductionHdiFactory factory;
    return factory;
}
