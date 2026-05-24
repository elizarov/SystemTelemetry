#include "telemetry/gpu/nvidia/impl/hdi_nvml.h"

#include <windows.h>

#include "util/resource_strings.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

constexpr unsigned int kNvmlTemperatureGpu = 0;
constexpr char kNvidiaMlLibraryName[] = "nvidia-ml.dll";
constexpr char kNvmlLibraryName[] = "nvml.dll";

struct NvmlMemory {
    unsigned long long total = 0;
    unsigned long long free = 0;
    unsigned long long used = 0;
};

struct NvmlFanSpeedInfo {
    unsigned int version = 0;
    unsigned int fan = 0;
    unsigned int speed = 0;
};

struct NvmlPciInfo {
    char busIdLegacy[16] = {};
    unsigned int domain = 0;
    unsigned int bus = 0;
    unsigned int device = 0;
    unsigned int pciDeviceId = 0;
    unsigned int pciSubSystemId = 0;
    char busId[32] = {};
};

constexpr unsigned int NvmlStructVersion(unsigned int size, unsigned int version) {
    return size | (version << 24);
}

constexpr unsigned int kNvmlFanSpeedInfoV1 = NvmlStructVersion(static_cast<unsigned int>(sizeof(NvmlFanSpeedInfo)), 1);

using NvmlInitFn = HdiNvmlReturn (*)();
using NvmlShutdownFn = HdiNvmlReturn (*)();
using NvmlErrorStringFn = const char* (*)(HdiNvmlReturn);
using NvmlDeviceGetCountFn = HdiNvmlReturn (*)(unsigned int*);
using NvmlDeviceGetHandleByIndexFn = HdiNvmlReturn (*)(unsigned int, HdiNvmlDevice*);
using NvmlDeviceGetNameFn = HdiNvmlReturn (*)(HdiNvmlDevice, char*, unsigned int);
using NvmlDeviceGetTemperatureFn = HdiNvmlReturn (*)(HdiNvmlDevice, unsigned int, unsigned int*);
using NvmlDeviceGetMemoryInfoFn = HdiNvmlReturn (*)(HdiNvmlDevice, NvmlMemory*);
using NvmlDeviceGetFanSpeedRpmFn = HdiNvmlReturn (*)(HdiNvmlDevice, NvmlFanSpeedInfo*);
using NvmlDeviceGetPciInfoFn = HdiNvmlReturn (*)(HdiNvmlDevice, NvmlPciInfo*);

class ProductionNvidiaNvmlHdi final : public NvidiaNvmlHdi {
public:
    explicit ProductionNvidiaNvmlHdi(Trace&) {}

    ~ProductionNvidiaNvmlHdi() override {
        if (initialized_ && shutdown_ != nullptr) {
            shutdown_();
        }
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    bool Load(std::string& diagnostics) override {
        module_ = LoadLibraryA(kNvmlLibraryName);
        if (module_ == nullptr) {
            module_ = LoadLibraryA(kNvidiaMlLibraryName);
        }
        if (module_ == nullptr) {
            diagnostics = ResourceStringText(RES_STR("NVML library not found."));
            return false;
        }

        bool loaded = true;
#define CASEDASH_LOAD_REQUIRED(function, name)                                                                         \
    function = reinterpret_cast<decltype(function)>(GetProcAddress(module_, name));                                    \
    loaded = function != nullptr && loaded
#define CASEDASH_LOAD_OPTIONAL(function, name)                                                                         \
    function = reinterpret_cast<decltype(function)>(GetProcAddress(module_, name))
        CASEDASH_LOAD_REQUIRED(init_, "nvmlInit_v2");
        CASEDASH_LOAD_REQUIRED(shutdown_, "nvmlShutdown");
        CASEDASH_LOAD_REQUIRED(errorString_, "nvmlErrorString");
        CASEDASH_LOAD_REQUIRED(deviceGetCount_, "nvmlDeviceGetCount_v2");
        CASEDASH_LOAD_REQUIRED(deviceGetHandleByIndex_, "nvmlDeviceGetHandleByIndex_v2");
        CASEDASH_LOAD_REQUIRED(deviceGetName_, "nvmlDeviceGetName");
        CASEDASH_LOAD_REQUIRED(deviceGetTemperature_, "nvmlDeviceGetTemperature");
        CASEDASH_LOAD_REQUIRED(deviceGetMemoryInfo_, "nvmlDeviceGetMemoryInfo");
        CASEDASH_LOAD_OPTIONAL(deviceGetFanSpeedRpm_, "nvmlDeviceGetFanSpeedRPM");
        CASEDASH_LOAD_OPTIONAL(deviceGetPciInfo_, "nvmlDeviceGetPciInfo_v3");
        if (deviceGetPciInfo_ == nullptr) {
            CASEDASH_LOAD_OPTIONAL(deviceGetPciInfo_, "nvmlDeviceGetPciInfo_v2");
        }
        if (deviceGetPciInfo_ == nullptr) {
            CASEDASH_LOAD_OPTIONAL(deviceGetPciInfo_, "nvmlDeviceGetPciInfo");
        }
#undef CASEDASH_LOAD_OPTIONAL
#undef CASEDASH_LOAD_REQUIRED

        if (!loaded) {
            diagnostics = ResourceStringText(RES_STR("NVML library is missing required entry points."));
            return false;
        }
        return true;
    }

    HdiNvmlReturn Initialize() override {
        const HdiNvmlReturn result = init_();
        initialized_ = result == kHdiNvmlSuccess;
        return result;
    }

    std::string ResultText(HdiNvmlReturn result) const override {
        if (errorString_ != nullptr) {
            const char* text = errorString_(result);
            if (text != nullptr && text[0] != '\0') {
                return text;
            }
        }
        return FormatText(RES_STR("%d"), result);
    }

    HdiNvmlReturn DeviceCount(unsigned int& count) const override {
        return deviceGetCount_(&count);
    }

    HdiNvmlReturn DeviceHandleByIndex(unsigned int index, HdiNvmlDevice& device) const override {
        return deviceGetHandleByIndex_(index, &device);
    }

    HdiNvmlReturn DeviceName(HdiNvmlDevice device, char* buffer, unsigned int bufferSize) const override {
        return deviceGetName_(device, buffer, bufferSize);
    }

    HdiNvmlReturn Temperature(HdiNvmlDevice device, unsigned int& temperatureC) const override {
        return deviceGetTemperature_(device, kNvmlTemperatureGpu, &temperatureC);
    }

    HdiNvmlReturn MemoryInfo(HdiNvmlDevice device, HdiNvmlMemory& memory) const override {
        NvmlMemory raw{};
        const HdiNvmlReturn result = deviceGetMemoryInfo_(device, &raw);
        memory.total = raw.total;
        memory.free = raw.free;
        memory.used = raw.used;
        return result;
    }

    std::optional<HdiNvmlReturn> FanSpeedRpm(HdiNvmlDevice device, unsigned int& fanRpm) const override {
        if (deviceGetFanSpeedRpm_ == nullptr) {
            return std::nullopt;
        }
        NvmlFanSpeedInfo info{};
        info.version = kNvmlFanSpeedInfoV1;
        info.fan = 0;
        const HdiNvmlReturn result = deviceGetFanSpeedRpm_(device, &info);
        fanRpm = info.speed;
        return result;
    }

    std::optional<HdiNvmlReturn> PciInfo(HdiNvmlDevice device, HdiNvmlPciInfo& pci) const override {
        if (deviceGetPciInfo_ == nullptr) {
            return std::nullopt;
        }
        NvmlPciInfo raw{};
        const HdiNvmlReturn result = deviceGetPciInfo_(device, &raw);
        pci.domain = raw.domain;
        pci.bus = raw.bus;
        pci.device = raw.device;
        pci.pciDeviceId = raw.pciDeviceId;
        pci.pciSubSystemId = raw.pciSubSystemId;
        return result;
    }

private:
    HMODULE module_ = nullptr;
    NvmlInitFn init_ = nullptr;
    NvmlShutdownFn shutdown_ = nullptr;
    NvmlErrorStringFn errorString_ = nullptr;
    NvmlDeviceGetCountFn deviceGetCount_ = nullptr;
    NvmlDeviceGetHandleByIndexFn deviceGetHandleByIndex_ = nullptr;
    NvmlDeviceGetNameFn deviceGetName_ = nullptr;
    NvmlDeviceGetTemperatureFn deviceGetTemperature_ = nullptr;
    NvmlDeviceGetMemoryInfoFn deviceGetMemoryInfo_ = nullptr;
    NvmlDeviceGetFanSpeedRpmFn deviceGetFanSpeedRpm_ = nullptr;
    NvmlDeviceGetPciInfoFn deviceGetPciInfo_ = nullptr;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<NvidiaNvmlHdi> CreateProductionNvidiaNvmlHdi(Trace& trace) {
    return std::make_unique<ProductionNvidiaNvmlHdi>(trace);
}
