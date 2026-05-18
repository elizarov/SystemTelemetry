#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"

#include <windows.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using NvmlDevice = void*;
using NvmlReturn = int;

constexpr NvmlReturn kNvmlSuccess = 0;
constexpr unsigned int kNvmlTemperatureGpu = 0;
constexpr wchar_t kNvidiaMlLibraryName[] = L"nvidia-ml.dll";  // LoadLibraryW requires a UTF-16 DLL name.
constexpr wchar_t kNvmlLibraryName[] = L"nvml.dll";           // LoadLibraryW requires a UTF-16 DLL name.
constexpr wchar_t kNvApiLibraryName[] = L"nvapi64.dll";       // LoadLibraryW requires a UTF-16 DLL name.

using NvApiStatus = int;
using NvApiPhysicalGpuHandle = void*;

constexpr NvApiStatus kNvApiOk = 0;
constexpr NvApiStatus kNvApiGpuNotPowered = -220;
constexpr int kNvApiMaxPhysicalGpus = 64;
constexpr int kNvApiShortStringSize = 64;
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

struct NvApiClockSample {
    NvApiStatus status = 0;
    std::optional<double> clockMhz;
};

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

using NvmlInitFn = NvmlReturn (*)();
using NvmlShutdownFn = NvmlReturn (*)();
using NvmlErrorStringFn = const char* (*)(NvmlReturn);
using NvmlDeviceGetCountFn = NvmlReturn (*)(unsigned int*);
using NvmlDeviceGetHandleByIndexFn = NvmlReturn (*)(unsigned int, NvmlDevice*);
using NvmlDeviceGetNameFn = NvmlReturn (*)(NvmlDevice, char*, unsigned int);
using NvmlDeviceGetTemperatureFn = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
using NvmlDeviceGetMemoryInfoFn = NvmlReturn (*)(NvmlDevice, NvmlMemory*);
using NvmlDeviceGetFanSpeedRpmFn = NvmlReturn (*)(NvmlDevice, NvmlFanSpeedInfo*);
using NvmlDeviceGetPciInfoFn = NvmlReturn (*)(NvmlDevice, NvmlPciInfo*);

using NvApiQueryInterfaceFn = void* (*)(unsigned int);
using NvApiInitializeFn = NvApiStatus (*)();
using NvApiUnloadFn = NvApiStatus (*)();
using NvApiEnumPhysicalGpusFn = NvApiStatus (*)(NvApiPhysicalGpuHandle*, int*);
using NvApiGpuGetFullNameFn = NvApiStatus (*)(NvApiPhysicalGpuHandle, char*);
using NvApiGpuGetAllClockFrequenciesFn = NvApiStatus (*)(NvApiPhysicalGpuHandle, NvApiClockFrequencies*);

class NvmlLibrary {
public:
    ~NvmlLibrary() {
        if (initialized_ && shutdown_ != nullptr) {
            shutdown_();
        }
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    bool Load(std::string& diagnostics) {
        module_ = LoadLibraryW(kNvmlLibraryName);
        if (module_ == nullptr) {
            module_ = LoadLibraryW(kNvidiaMlLibraryName);
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

    NvmlReturn Initialize() {
        const NvmlReturn result = init_();
        initialized_ = result == kNvmlSuccess;
        return result;
    }

    std::string ResultText(NvmlReturn result) const {
        if (errorString_ != nullptr) {
            const char* text = errorString_(result);
            if (text != nullptr && text[0] != '\0') {
                return Utf8FromAnsi(text);
            }
        }
        return FormatText(RES_STR("%d"), result);
    }

    NvmlReturn DeviceCount(unsigned int& count) const {
        return deviceGetCount_(&count);
    }

    NvmlReturn DeviceHandleByIndex(unsigned int index, NvmlDevice& device) const {
        return deviceGetHandleByIndex_(index, &device);
    }

    NvmlReturn DeviceName(NvmlDevice device, char* buffer, unsigned int bufferSize) const {
        return deviceGetName_(device, buffer, bufferSize);
    }

    NvmlReturn Temperature(NvmlDevice device, unsigned int& temperatureC) const {
        return deviceGetTemperature_(device, kNvmlTemperatureGpu, &temperatureC);
    }

    NvmlReturn MemoryInfo(NvmlDevice device, NvmlMemory& memory) const {
        return deviceGetMemoryInfo_(device, &memory);
    }

    std::optional<NvmlReturn> FanSpeedRpm(NvmlDevice device, unsigned int& fanRpm) const {
        if (deviceGetFanSpeedRpm_ == nullptr) {
            return std::nullopt;
        }
        NvmlFanSpeedInfo info{};
        info.version = kNvmlFanSpeedInfoV1;
        info.fan = 0;
        const NvmlReturn result = deviceGetFanSpeedRpm_(device, &info);
        fanRpm = info.speed;
        return result;
    }

    std::optional<NvmlReturn> PciInfo(NvmlDevice device, NvmlPciInfo& pci) const {
        if (deviceGetPciInfo_ == nullptr) {
            return std::nullopt;
        }
        pci = NvmlPciInfo{};
        return deviceGetPciInfo_(device, &pci);
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

class NvApiLibrary {
public:
    ~NvApiLibrary() {
        if (initialized_ && unload_ != nullptr) {
            unload_();
        }
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    bool Initialize(const std::string& preferredName, Trace& trace, std::string& diagnostics) {
        module_ = LoadLibraryW(kNvApiLibraryName);
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

        const NvApiStatus initStatus = initialize_();
        trace.WriteFmt(TracePrefix::NvidiaNvml, RES_STR("nvapi_init result=\"%s\""), ResultText(initStatus).c_str());
        if (initStatus != kNvApiOk) {
            diagnostics = FormatText(RES_STR("NVAPI initialization failed: %s"), ResultText(initStatus).c_str());
            return false;
        }
        initialized_ = true;

        NvApiPhysicalGpuHandle handles[kNvApiMaxPhysicalGpus] = {};
        int count = 0;
        const NvApiStatus enumStatus = enumPhysicalGpus_(handles, &count);
        trace.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("nvapi_enum result=\"%s\" count=%d"),
            ResultText(enumStatus).c_str(),
            count);
        if (enumStatus != kNvApiOk || count <= 0) {
            diagnostics = FormatText(RES_STR("NVAPI found no NVIDIA GPUs: %s"), ResultText(enumStatus).c_str());
            return false;
        }

        int bestRank = -1;
        for (int index = 0; index < count && index < kNvApiMaxPhysicalGpus; ++index) {
            if (handles[index] == nullptr) {
                continue;
            }
            char name[kNvApiShortStringSize] = {};
            const NvApiStatus nameStatus = gpuGetFullName_(handles[index], name);
            const std::string candidateName = nameStatus == kNvApiOk ? Utf8FromAnsi(name) : std::string();
            const int rank = !preferredName.empty() && EqualsInsensitive(candidateName, preferredName)
                                 ? 3
                                 : (!preferredName.empty() && (ContainsInsensitive(candidateName, preferredName) ||
                                                                  ContainsInsensitive(preferredName, candidateName))
                                           ? 2
                                           : 1);
            trace.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("nvapi_device_candidate index=%d name_result=\"%s\" match_rank=%d name=\"%s\""),
                index,
                ResultText(nameStatus).c_str(),
                rank,
                candidateName.c_str());
            if (nameStatus == kNvApiOk && rank > bestRank) {
                bestRank = rank;
                physicalGpu_ = handles[index];
                gpuName_ = candidateName;
            }
        }

        if (physicalGpu_ == nullptr) {
            diagnostics = ResourceStringText(RES_STR("NVAPI failed to select a physical GPU."));
            return false;
        }

        trace.WriteFmt(
            TracePrefix::NvidiaNvml, RES_STR("nvapi_device_selected rank=%d name=\"%s\""), bestRank, gpuName_.c_str());
        diagnostics = FormatText(RES_STR("NVAPI clock GPU=%s"), gpuName_.c_str());
        return true;
    }

    NvApiClockSample GraphicsClock() const {
        NvApiClockSample sample;
        if (!initialized_ || physicalGpu_ == nullptr || gpuGetAllClockFrequencies_ == nullptr) {
            sample.status = -1;
            return sample;
        }

        NvApiClockFrequencies frequencies{};
        frequencies.version = kNvApiClockFrequenciesV3;
        frequencies.clockType = kNvApiClockFrequenciesCurrent;
        sample.status = gpuGetAllClockFrequencies_(physicalGpu_, &frequencies);
        const NvApiClockDomain& graphics = frequencies.domain[kNvApiPublicClockGraphics];
        if (sample.status == kNvApiOk && graphics.bIsPresent != 0 && graphics.frequency > 0) {
            sample.clockMhz = static_cast<double>(graphics.frequency) / 1000.0;
        }
        return sample;
    }

    std::string ResultText(NvApiStatus status) const {
        switch (status) {
            case kNvApiOk:
                return ResourceStringText(RES_STR("OK"));
            case kNvApiGpuNotPowered:
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
    NvApiPhysicalGpuHandle physicalGpu_ = nullptr;
    std::string gpuName_;
    bool initialized_ = false;
};

std::string KnownNvmlName(const std::array<char, 128>& name) {
    return name[0] != '\0' ? Utf8FromAnsi(name.data()) : std::string();
}

bool NvmlPackedDeviceIdMatches(unsigned int pciDeviceId, const GpuAdapterInfo& adapter) {
    const unsigned int low = pciDeviceId & 0xffffu;
    const unsigned int high = (pciDeviceId >> 16) & 0xffffu;
    return (low == adapter.vendorId && high == adapter.deviceId) ||
           (low == adapter.deviceId && high == adapter.vendorId);
}

int NvidiaDeviceMatchRank(const GpuAdapterInfo& adapter, const NvmlPciInfo* pci, const std::string& name) {
    if (pci != nullptr && adapter.hasPciAddress && pci->domain == adapter.pciDomain && pci->bus == adapter.pciBus &&
        pci->device == adapter.pciDevice) {
        return 5;
    }
    if (pci != nullptr && NvmlPackedDeviceIdMatches(pci->pciDeviceId, adapter) &&
        (adapter.subSysId == 0 || pci->pciSubSystemId == adapter.subSysId)) {
        return 4;
    }
    if (!adapter.adapterName.empty()) {
        if (EqualsInsensitive(name, adapter.adapterName)) {
            return 3;
        }
        if (ContainsInsensitive(name, adapter.adapterName) || ContainsInsensitive(adapter.adapterName, name)) {
            return 2;
        }
    }
    return 1;
}

class NvidiaNvmlGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    NvidiaNvmlGpuTelemetryProvider(Trace& trace, std::optional<GpuAdapterInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    bool Initialize() override {
        trace_.Write(TracePrefix::NvidiaNvml, RES_STR("initialize_begin"));
        if (!nvml_.Load(diagnostics_)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml, RES_STR("load_failed diagnostics=\"%s\""), diagnostics_.c_str());
            return false;
        }

        NvmlReturn result = nvml_.Initialize();
        trace_.WriteFmt(TracePrefix::NvidiaNvml, RES_STR("init_done result=\"%s\""), nvml_.ResultText(result).c_str());
        if (result != kNvmlSuccess) {
            diagnostics_ = FormatText(RES_STR("NVML initialization failed: %s"), nvml_.ResultText(result).c_str());
            return false;
        }

        unsigned int deviceCount = 0;
        result = nvml_.DeviceCount(deviceCount);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("get_count result=\"%s\" count=%u"),
            nvml_.ResultText(result).c_str(),
            deviceCount);
        if (result != kNvmlSuccess || deviceCount == 0) {
            diagnostics_ = FormatText(RES_STR("NVML found no NVIDIA GPUs: count=%s"), nvml_.ResultText(result).c_str());
            return false;
        }

        if (!SelectDevice(deviceCount)) {
            return false;
        }

        if (gpuName_.empty()) {
            gpuName_ = "NVIDIA GPU";
        }

        NvmlMemory memory{};
        const NvmlReturn memoryResult = nvml_.MemoryInfo(device_, memory);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("get_total_vram result=\"%s\" total_bytes=%llu"),
            nvml_.ResultText(memoryResult).c_str(),
            static_cast<unsigned long long>(memory.total));
        if (memoryResult == kNvmlSuccess && memory.total > 0) {
            totalVramGb_ = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
        }

        fanRpmSupported_ = DetectFanSpeedRpm();
        nvapiClockAvailable_ = nvapi_.Initialize(gpuName_, trace_, clockDiagnostics_);
        diagnostics_ = FormatText(
            RES_STR("NVML GPU=%s load_source=pdh clock_source=%s fan_rpm_supported=%s native_fps_supported=no"),
            gpuName_.c_str(),
            nvapiClockAvailable_ ? "nvapi" : "unavailable",
            fanRpmSupported_ ? "yes" : "no");
        fpsProvider_ = CreatePresentedFpsProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider active."));
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ = fpsSample.diagnostics.empty()
                                  ? ResourceStringText(RES_STR("Presented FPS ETW provider unavailable."))
                                  : fpsSample.diagnostics;
        }
        initialized_ = true;
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("initialize_done diagnostics=\"%s\" fps=\"%s\""),
            diagnostics_.c_str(),
            fpsDiagnostics_.c_str());
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace_.Write(TracePrefix::NvidiaNvml, RES_STR("sample_begin"));
        GpuVendorTelemetrySample sample = CreateBaseSample();

        if (!initialized_ || device_ == nullptr) {
            sample.available = false;
            return sample;
        }

        bool hasAnyMetric = false;

        unsigned int temperatureC = 0;
        NvmlReturn result = nvml_.Temperature(device_, temperatureC);
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("get_temperature result=\"%s\" value=%u"),
                nvml_.ResultText(result).c_str(),
                temperatureC);
        }
        if (result == kNvmlSuccess) {
            sample.temperatureC = static_cast<double>(temperatureC);
            hasAnyMetric = true;
        }

        if (nvapiClockAvailable_) {
            const NvApiClockSample clockSample = nvapi_.GraphicsClock();
            if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
                const double clockMhz = clockSample.clockMhz.value_or(0.0);
                trace_.WriteFmt(TracePrefix::NvidiaNvml,
                    RES_STR("get_clock_nvapi result=\"%s\" value_mhz=%.1f"),
                    nvapi_.ResultText(clockSample.status).c_str(),
                    clockMhz);
            }
            if (clockSample.clockMhz.has_value()) {
                sample.coreClockMhz = *clockSample.clockMhz;
                hasAnyMetric = true;
            }
        }

        NvmlMemory memory{};
        result = nvml_.MemoryInfo(device_, memory);
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("get_memory result=\"%s\" used_bytes=%llu total_bytes=%llu"),
                nvml_.ResultText(result).c_str(),
                static_cast<unsigned long long>(memory.used),
                static_cast<unsigned long long>(memory.total));
        }
        if (result == kNvmlSuccess) {
            sample.usedVramGb = static_cast<double>(memory.used) / (1024.0 * 1024.0 * 1024.0);
            if (memory.total > 0) {
                sample.totalVramGb = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
            }
            hasAnyMetric = true;
        }

        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> fanResult =
            fanRpmSupported_ ? nvml_.FanSpeedRpm(device_, fanRpm) : std::nullopt;
        if (fanRpmSupported_ && fanResult.has_value()) {
            if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
                trace_.WriteFmt(TracePrefix::NvidiaNvml,
                    RES_STR("get_fan_rpm result=\"%s\" value=%u"),
                    nvml_.ResultText(*fanResult).c_str(),
                    fanRpm);
            }
        } else {
            trace_.Write(TracePrefix::NvidiaNvml, RES_STR("get_fan_rpm unavailable"));
        }
        if (fanResult.has_value() && *fanResult == kNvmlSuccess) {
            sample.fanRpm = static_cast<double>(fanRpm);
            hasAnyMetric = true;
        }

        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            sample.fpsPermissionRequired = fpsSample.permissionRequired;
            sample.fpsAppName = fpsSample.processName;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                hasAnyMetric = true;
            }
            if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
                const std::string fpsText =
                    fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1) : "fps=N/A";
                trace_.WriteFmt(TracePrefix::NvidiaNvml,
                    RES_STR("get_presented_fps available=%s value=%s process=\"%s\" diagnostics=\"%s\""),
                    Trace::BoolText(fpsSample.fps.has_value()),
                    fpsText.c_str(),
                    fpsSample.processName.c_str(),
                    fpsSample.diagnostics.c_str());
            }
        }

        sample.available = hasAnyMetric;
        AppendFormat(sample.diagnostics, RES_STR(" fps=%s"), fpsDiagnostics_.c_str());
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("sample_done available=%s diagnostics=\"%s\""),
            Trace::BoolText(sample.available),
            sample.diagnostics.c_str());
        return sample;
    }

private:
    GpuVendorTelemetrySample CreateBaseSample() const {
        GpuVendorTelemetrySample sample;
        sample.providerName = "NVIDIA NVML";
        sample.name = gpuName_;
        sample.totalVramGb = totalVramGb_;
        sample.diagnostics = diagnostics_;
        return sample;
    }

    bool SelectDevice(unsigned int deviceCount) {
        int bestRank = -1;
        NvmlDevice bestDevice = nullptr;
        std::string bestName;
        std::string bestMatch = "fallback";
        NvmlReturn bestResult = kNvmlSuccess;

        for (unsigned int index = 0; index < deviceCount; ++index) {
            NvmlDevice candidate = nullptr;
            const NvmlReturn handleResult = nvml_.DeviceHandleByIndex(index, candidate);
            std::array<char, 128> name{};
            const NvmlReturn nameResult =
                candidate != nullptr ? nvml_.DeviceName(candidate, name.data(), static_cast<unsigned int>(name.size()))
                                     : handleResult;
            NvmlPciInfo pci{};
            const std::optional<NvmlReturn> pciResult =
                candidate != nullptr ? nvml_.PciInfo(candidate, pci) : std::nullopt;
            const bool pciOk = pciResult.has_value() && *pciResult == kNvmlSuccess;
            const std::string candidateName = nameResult == kNvmlSuccess ? KnownNvmlName(name) : std::string();
            const int rank = candidate != nullptr && adapter_.has_value()
                                 ? NvidiaDeviceMatchRank(*adapter_, pciOk ? &pci : nullptr, candidateName)
                                 : (candidate != nullptr ? 1 : 0);
            const std::string pciResultText =
                pciResult.has_value() ? nvml_.ResultText(*pciResult) : std::string("unavailable");
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("device_candidate index=%u handle_result=\"%s\" name_result=\"%s\" pci_result=\"%s\" "
                        "pci=%u:%u:%u pci_device_id=0x%08X subsystem_id=0x%08X match_rank=%d name=\"%s\""),
                index,
                nvml_.ResultText(handleResult).c_str(),
                nvml_.ResultText(nameResult).c_str(),
                pciResultText.c_str(),
                pci.domain,
                pci.bus,
                pci.device,
                pci.pciDeviceId,
                pci.pciSubSystemId,
                rank,
                candidateName.c_str());
            if (candidate != nullptr && rank > bestRank) {
                bestRank = rank;
                bestDevice = candidate;
                bestName = candidateName;
                bestResult = handleResult;
                bestMatch = rank >= 5 ? "pci" : (rank >= 4 ? "device_id" : (rank >= 2 ? "name" : "fallback"));
            }
        }

        device_ = bestDevice;
        gpuName_ = bestMatch == "pci" && adapter_.has_value() && !adapter_->adapterName.empty() ? adapter_->adapterName
                                                                                                : bestName;
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("device_selected match=\"%s\" rank=%d display_name=\"%s\" selected_adapter=\"%s\""),
            bestMatch.c_str(),
            bestRank,
            gpuName_.c_str(),
            adapter_.has_value() ? adapter_->adapterName.c_str() : "");
        if (device_ == nullptr) {
            diagnostics_ = FormatText(
                RES_STR("NVML failed to open selected NVIDIA GPU: device=%s"), nvml_.ResultText(bestResult).c_str());
            return false;
        }
        return true;
    }

    bool DetectFanSpeedRpm() const {
        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> result = device_ != nullptr ? nvml_.FanSpeedRpm(device_, fanRpm) : std::nullopt;
        return result.has_value() && *result == kNvmlSuccess;
    }

    Trace& trace_;
    NvmlLibrary nvml_;
    NvApiLibrary nvapi_;
    NvmlDevice device_ = nullptr;
    std::optional<GpuAdapterInfo> adapter_;
    std::string gpuName_;
    std::string diagnostics_ = ResourceStringText(RES_STR("NVML provider not initialized."));
    std::string clockDiagnostics_ = ResourceStringText(RES_STR("NVAPI clock provider not initialized."));
    std::string fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider not initialized."));
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool nvapiClockAvailable_ = false;
    bool fanRpmSupported_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(
    Trace& trace, std::optional<GpuAdapterInfo> adapter) {
    return std::make_unique<NvidiaNvmlGpuTelemetryProvider>(trace, std::move(adapter));
}
