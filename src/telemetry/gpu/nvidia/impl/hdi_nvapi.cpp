#include "telemetry/gpu/nvidia/impl/hdi_nvapi.h"

#include <windows.h>

#include <algorithm>

#include "util/resource_strings.h"
#include "util/text_format.h"

namespace {

constexpr char kNvApiLibraryName[] = "nvapi64.dll";
constexpr int kNvApiMaxPhysicalGpus = 64;
constexpr int kNvApiMaxGpuPublicClocks = 32;
constexpr int kNvApiPublicClockGraphics = 0;
constexpr unsigned int kNvApiClockFrequenciesCurrent = 0;
constexpr unsigned int kNvApiInitialize = 0x0150E828;
constexpr unsigned int kNvApiUnload = 0xD22BDD7E;
constexpr unsigned int kNvApiEnumPhysicalGpus = 0xE5AC921F;
constexpr unsigned int kNvApiGpuGetFullName = 0xCEEE8E9F;
constexpr unsigned int kNvApiGpuGetAllClockFrequencies = 0xDCB616C3;

constexpr unsigned int NvApiStructVersion(unsigned int size, unsigned int version) {
    return size | (version << 16);
}

struct NvApiClockDomain {
    unsigned int bIsPresent : 1;
    unsigned int reserved : 31;
    unsigned int frequency = 0;
};

struct NvApiClockFrequencies {
    unsigned int version = 0;
    unsigned int clockType : 4;
    unsigned int reserved : 20;
    unsigned int reserved1 : 8;
    NvApiClockDomain domain[kNvApiMaxGpuPublicClocks]{};
};

constexpr unsigned int kNvApiClockFrequenciesV3 =
    NvApiStructVersion(static_cast<unsigned int>(sizeof(NvApiClockFrequencies)), 3);

using NvApiQueryInterfaceFn = void* (*)(unsigned int);
using NvApiInitializeFn = HdiNvapiStatus (*)();
using NvApiUnloadFn = HdiNvapiStatus (*)();
using NvApiEnumPhysicalGpusFn = HdiNvapiStatus (*)(HdiNvapiPhysicalGpuHandle*, int*);
using NvApiGpuGetFullNameFn = HdiNvapiStatus (*)(HdiNvapiPhysicalGpuHandle, char*);
using NvApiGpuGetAllClockFrequenciesFn = HdiNvapiStatus (*)(HdiNvapiPhysicalGpuHandle, NvApiClockFrequencies*);

void CopyNvapiString(char* buffer, unsigned int bufferSize, const char* text) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    unsigned int index = 0;
    while (index + 1 < bufferSize && text[index] != '\0') {
        buffer[index] = text[index];
        ++index;
    }
    buffer[index] = '\0';
}

class ProductionNvidiaNvapiHdi final : public NvidiaNvapiHdi {
public:
    explicit ProductionNvidiaNvapiHdi(Trace&) {}

    ~ProductionNvidiaNvapiHdi() override {
        if (initialized_ && unload_ != nullptr) {
            unload_();
        }
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    bool Load(std::string& diagnostics) override {
        module_ = LoadLibraryA(kNvApiLibraryName);
        if (module_ == nullptr) {
            diagnostics = ResourceStringText(RES_STR("NVAPI library not found."));
            return false;
        }

        queryInterface_ = reinterpret_cast<NvApiQueryInterfaceFn>(GetProcAddress(module_, "nvapi_QueryInterface"));
        if (queryInterface_ == nullptr) {
            diagnostics = ResourceStringText(RES_STR("NVAPI query interface not found."));
            return false;
        }

        initialize_ = Query<NvApiInitializeFn>(kNvApiInitialize);
        unload_ = Query<NvApiUnloadFn>(kNvApiUnload);
        enumPhysicalGpus_ = Query<NvApiEnumPhysicalGpusFn>(kNvApiEnumPhysicalGpus);
        gpuGetFullName_ = Query<NvApiGpuGetFullNameFn>(kNvApiGpuGetFullName);
        gpuGetAllClockFrequencies_ = Query<NvApiGpuGetAllClockFrequenciesFn>(kNvApiGpuGetAllClockFrequencies);
        if (initialize_ == nullptr || enumPhysicalGpus_ == nullptr || gpuGetFullName_ == nullptr ||
            gpuGetAllClockFrequencies_ == nullptr) {
            diagnostics = ResourceStringText(RES_STR("NVAPI library is missing required entry points."));
            return false;
        }
        return true;
    }

    HdiNvapiStatus Initialize() override {
        const HdiNvapiStatus result = initialize_();
        initialized_ = result == kHdiNvapiOk;
        return result;
    }

    HdiNvapiStatus EnumPhysicalGpus(std::vector<HdiNvapiPhysicalGpuHandle>& handles) override {
        handles.clear();
        HdiNvapiPhysicalGpuHandle rawHandles[kNvApiMaxPhysicalGpus] = {};
        int count = 0;
        const HdiNvapiStatus result = enumPhysicalGpus_(rawHandles, &count);
        if (result == kHdiNvapiOk && count > 0) {
            const int safeCount = std::min(count, kNvApiMaxPhysicalGpus);
            handles.assign(rawHandles, rawHandles + safeCount);
        }
        return result;
    }

    HdiNvapiStatus GpuFullName(HdiNvapiPhysicalGpuHandle handle, char* buffer, unsigned int bufferSize) override {
        if (buffer != nullptr && bufferSize > 0) {
            buffer[0] = '\0';
        }
        char name[kHdiNvapiShortStringSize] = {};
        const HdiNvapiStatus result = gpuGetFullName_(handle, name);
        CopyNvapiString(buffer, bufferSize, name);
        return result;
    }

    HdiNvapiClockFrequencies GraphicsClock(HdiNvapiPhysicalGpuHandle handle) const override {
        HdiNvapiClockFrequencies sample;
        if (!initialized_ || handle == nullptr || gpuGetAllClockFrequencies_ == nullptr) {
            sample.status = -1;
            return sample;
        }

        NvApiClockFrequencies frequencies{};
        frequencies.version = kNvApiClockFrequenciesV3;
        frequencies.clockType = kNvApiClockFrequenciesCurrent;
        sample.status = gpuGetAllClockFrequencies_(handle, &frequencies);
        const NvApiClockDomain& graphics = frequencies.domain[kNvApiPublicClockGraphics];
        sample.graphicsPresent = sample.status == kHdiNvapiOk && graphics.bIsPresent != 0;
        sample.graphicsFrequencyKhz = graphics.frequency;
        return sample;
    }

    std::string ResultText(HdiNvapiStatus status) const override {
        switch (status) {
            case kHdiNvapiOk:
                return ResourceStringText(RES_STR("OK"));
            case kHdiNvapiGpuNotPowered:
                return ResourceStringText(RES_STR("GPU not powered"));
            default:
                return FormatText(RES_STR("%d"), status);
        }
    }

private:
    template <typename Fn> Fn Query(unsigned int id) const {
        return queryInterface_ != nullptr ? reinterpret_cast<Fn>(queryInterface_(id)) : nullptr;
    }

    HMODULE module_ = nullptr;
    NvApiQueryInterfaceFn queryInterface_ = nullptr;
    NvApiInitializeFn initialize_ = nullptr;
    NvApiUnloadFn unload_ = nullptr;
    NvApiEnumPhysicalGpusFn enumPhysicalGpus_ = nullptr;
    NvApiGpuGetFullNameFn gpuGetFullName_ = nullptr;
    NvApiGpuGetAllClockFrequenciesFn gpuGetAllClockFrequencies_ = nullptr;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<NvidiaNvapiHdi> CreateProductionNvidiaNvapiHdi(Trace& trace) {
    return std::make_unique<ProductionNvidiaNvapiHdi>(trace);
}
