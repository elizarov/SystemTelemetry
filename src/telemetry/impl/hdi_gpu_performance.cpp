#include "telemetry/impl/hdi_gpu_performance.h"

#include <windows.h>

#include <cstring>
#include <pdh.h>
#include <pdhmsg.h>
#include <string_view>
#include <vector>

#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"
#include "util/resource_strings.h"
#include "util/trace.h"

namespace {

constexpr char kGpuEngine3dMarker[] = "engtype_3D";

char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool MatchesPdhInstanceFilter(const char* instance, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    if (instance == nullptr) {
        return false;
    }

    for (const char* cursor = instance; *cursor != '\0'; ++cursor) {
        size_t matched = 0;
        while (matched < filter.size() && cursor[matched] != '\0' &&
               LowerAscii(cursor[matched]) == LowerAscii(filter[matched])) {
            ++matched;
        }
        if (matched == filter.size()) {
            return true;
        }
    }
    return false;
}

class ProductionGpuPerformanceHdi final : public GpuPerformanceHdi {
public:
    explicit ProductionGpuPerformanceHdi(Trace& trace) : trace_(trace) {}

    ~ProductionGpuPerformanceHdi() override {
        if (loadQuery_ != nullptr) {
            PdhCloseQuery(loadQuery_);
        }
        if (memoryQuery_ != nullptr) {
            PdhCloseQuery(memoryQuery_);
        }
    }

    void Initialize() override {
        const PDH_STATUS queryStatus = PdhOpenQueryA(nullptr, 0, &loadQuery_);
        trace_.WriteFmt(
            TracePrefix::Telemetry, RES_STR("pdh_open gpu_query status=%ld"), static_cast<long>(queryStatus));
        const PDH_STATUS loadStatus =
            AddCounterCompat(loadQuery_, "\\GPU Engine(*)\\Utilization Percentage", &loadCounter_);
        trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" status=%ld"),
            static_cast<long>(loadStatus));
        const PDH_STATUS collectStatus = PdhCollectQueryData(loadQuery_);
        trace_.WriteFmt(
            TracePrefix::Telemetry, RES_STR("pdh_collect gpu_query status=%ld"), static_cast<long>(collectStatus));

        const PDH_STATUS memoryQueryStatus = PdhOpenQueryA(nullptr, 0, &memoryQuery_);
        trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("pdh_open gpu_memory_query status=%ld"),
            static_cast<long>(memoryQueryStatus));
        const PDH_STATUS memoryCounterStatus =
            AddCounterCompat(memoryQuery_, "\\GPU Adapter Memory(*)\\Dedicated Usage", &dedicatedCounter_);
        trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" status=%ld"),
            static_cast<long>(memoryCounterStatus));
        const PDH_STATUS memoryCollectStatus = PdhCollectQueryData(memoryQuery_);
        trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("pdh_collect gpu_memory_query status=%ld"),
            static_cast<long>(memoryCollectStatus));
    }

    GpuPerformanceLoadSample SampleLoad(std::string_view instanceFilter, const char* filterLabel) override {
        GpuPerformanceLoadSample sample;
        if (loadQuery_ == nullptr || loadCounter_ == nullptr) {
            return sample;
        }

        const PDH_STATUS collectStatus = PdhCollectQueryData(loadQuery_);
        trace_.WriteFmt(TracePrefix::Telemetry, RES_STR("gpu_collect status=%ld"), static_cast<long>(collectStatus));
        sample = ReadCounterArrayTotals(loadCounter_, instanceFilter, filterLabel);
        sample.total = FiniteNonNegativeOr(sample.total);
        sample.total3d = FiniteNonNegativeOr(sample.total3d);
        return sample;
    }

    double SampleDedicatedMemoryBytes(std::string_view instanceFilter, const char* filterLabel) override {
        if (memoryQuery_ == nullptr || dedicatedCounter_ == nullptr) {
            return 0.0;
        }

        const PDH_STATUS collectStatus = PdhCollectQueryData(memoryQuery_);
        trace_.WriteFmt(
            TracePrefix::Telemetry, RES_STR("gpu_memory_collect status=%ld"), static_cast<long>(collectStatus));
        return FiniteNonNegativeOr(ReadCounterArrayTotals(dedicatedCounter_, instanceFilter, filterLabel).total);
    }

private:
    GpuPerformanceLoadSample ReadCounterArrayTotals(
        PDH_HCOUNTER counter, std::string_view instanceFilter, const char* filterLabel) {
        GpuPerformanceLoadSample sample;
        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayA(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (status != PDH_MORE_DATA) {
            trace_.WriteFmt(TracePrefix::Telemetry, RES_STR("pdh_array_prepare status=%ld"), static_cast<long>(status));
            return sample;
        }

        counterArrayBuffer_.resize(bufferSize);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(counterArrayBuffer_.data());
        status = PdhGetFormattedCounterArrayA(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS) {
            trace_.WriteFmt(TracePrefix::Telemetry,
                RES_STR("pdh_array_fetch status=%ld count=%lu"),
                static_cast<long>(status),
                static_cast<unsigned long>(itemCount));
            return sample;
        }

        for (DWORD i = 0; i < itemCount; ++i) {
            const char* instance = items[i].szName;
            if (!MatchesPdhInstanceFilter(instance, instanceFilter)) {
                continue;
            }
            if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !IsFiniteDouble(items[i].FmtValue.doubleValue)) {
                continue;
            }
            ++sample.matchedCount;
            sample.total += items[i].FmtValue.doubleValue;
            if (instance != nullptr && std::strstr(instance, kGpuEngine3dMarker) != nullptr) {
                sample.total3d += items[i].FmtValue.doubleValue;
            }
        }

        trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR(
                "pdh_array_done status=%ld count=%lu matched=%lu filter=\"%s\" total=value=%.2f total3d=value=%.2f"),
            static_cast<long>(status),
            static_cast<unsigned long>(itemCount),
            sample.matchedCount,
            filterLabel,
            sample.total,
            sample.total3d);
        return sample;
    }

    Trace& trace_;
    PDH_HQUERY loadQuery_ = nullptr;
    PDH_HCOUNTER loadCounter_ = nullptr;
    PDH_HQUERY memoryQuery_ = nullptr;
    PDH_HCOUNTER dedicatedCounter_ = nullptr;
    std::vector<BYTE> counterArrayBuffer_;
};

}  // namespace

std::unique_ptr<GpuPerformanceHdi> CreateProductionGpuPerformanceHdi(Trace& trace) {
    return std::make_unique<ProductionGpuPerformanceHdi>(trace);
}
