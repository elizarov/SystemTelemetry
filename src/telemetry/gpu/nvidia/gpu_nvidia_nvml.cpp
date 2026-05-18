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
constexpr unsigned int kNvmlClockGraphics = 0;
constexpr wchar_t kNvidiaMlLibraryName[] = L"nvidia-ml.dll";  // LoadLibraryW requires a UTF-16 DLL name.
constexpr wchar_t kNvmlLibraryName[] = L"nvml.dll";           // LoadLibraryW requires a UTF-16 DLL name.

struct NvmlUtilization {
    unsigned int gpu = 0;
    unsigned int memory = 0;
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
using NvmlDeviceGetUtilizationRatesFn = NvmlReturn (*)(NvmlDevice, NvmlUtilization*);
using NvmlDeviceGetTemperatureFn = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
using NvmlDeviceGetClockInfoFn = NvmlReturn (*)(NvmlDevice, unsigned int, unsigned int*);
using NvmlDeviceGetMemoryInfoFn = NvmlReturn (*)(NvmlDevice, NvmlMemory*);
using NvmlDeviceGetFanSpeedRpmFn = NvmlReturn (*)(NvmlDevice, NvmlFanSpeedInfo*);
using NvmlDeviceGetPciInfoFn = NvmlReturn (*)(NvmlDevice, NvmlPciInfo*);

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
        CASEDASH_LOAD_REQUIRED(deviceGetUtilizationRates_, "nvmlDeviceGetUtilizationRates");
        CASEDASH_LOAD_REQUIRED(deviceGetTemperature_, "nvmlDeviceGetTemperature");
        CASEDASH_LOAD_REQUIRED(deviceGetClockInfo_, "nvmlDeviceGetClockInfo");
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
        return FormatText("%d", result);
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

    NvmlReturn UtilizationRates(NvmlDevice device, NvmlUtilization& utilization) const {
        return deviceGetUtilizationRates_(device, &utilization);
    }

    NvmlReturn Temperature(NvmlDevice device, unsigned int& temperatureC) const {
        return deviceGetTemperature_(device, kNvmlTemperatureGpu, &temperatureC);
    }

    NvmlReturn GraphicsClock(NvmlDevice device, unsigned int& clockMhz) const {
        return deviceGetClockInfo_(device, kNvmlClockGraphics, &clockMhz);
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
    NvmlDeviceGetUtilizationRatesFn deviceGetUtilizationRates_ = nullptr;
    NvmlDeviceGetTemperatureFn deviceGetTemperature_ = nullptr;
    NvmlDeviceGetClockInfoFn deviceGetClockInfo_ = nullptr;
    NvmlDeviceGetMemoryInfoFn deviceGetMemoryInfo_ = nullptr;
    NvmlDeviceGetFanSpeedRpmFn deviceGetFanSpeedRpm_ = nullptr;
    NvmlDeviceGetPciInfoFn deviceGetPciInfo_ = nullptr;
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

        diagnostics_ = FormatText(RES_STR("NVML GPU=%s fan_rpm_supported=%s native_fps_supported=no"),
            gpuName_.c_str(),
            HasFanSpeedRpm() ? "yes" : "no");
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
        GpuVendorTelemetrySample sample;
        sample.providerName = "NVIDIA NVML";
        sample.name = gpuName_;
        sample.totalVramGb = totalVramGb_;
        sample.diagnostics = diagnostics_;

        if (!initialized_ || device_ == nullptr) {
            sample.available = false;
            return sample;
        }

        bool hasAnyMetric = false;

        NvmlUtilization utilization{};
        NvmlReturn result = nvml_.UtilizationRates(device_, utilization);
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("get_utilization result=\"%s\" gpu=%u"),
                nvml_.ResultText(result).c_str(),
                utilization.gpu);
        }
        if (result == kNvmlSuccess) {
            sample.loadPercent = static_cast<double>(utilization.gpu);
            hasAnyMetric = true;
        }

        unsigned int temperatureC = 0;
        result = nvml_.Temperature(device_, temperatureC);
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

        unsigned int clockMhz = 0;
        result = nvml_.GraphicsClock(device_, clockMhz);
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("get_clock result=\"%s\" value=%u"),
                nvml_.ResultText(result).c_str(),
                clockMhz);
        }
        if (result == kNvmlSuccess) {
            sample.coreClockMhz = static_cast<double>(clockMhz);
            hasAnyMetric = true;
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
        const std::optional<NvmlReturn> fanResult = nvml_.FanSpeedRpm(device_, fanRpm);
        if (fanResult.has_value()) {
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

    bool HasFanSpeedRpm() const {
        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> result = device_ != nullptr ? nvml_.FanSpeedRpm(device_, fanRpm) : std::nullopt;
        return result.has_value() && *result == kNvmlSuccess;
    }

    Trace& trace_;
    NvmlLibrary nvml_;
    NvmlDevice device_ = nullptr;
    std::optional<GpuAdapterInfo> adapter_;
    std::string gpuName_;
    std::string diagnostics_ = ResourceStringText(RES_STR("NVML provider not initialized."));
    std::string fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider not initialized."));
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(
    Trace& trace, std::optional<GpuAdapterInfo> adapter) {
    return std::make_unique<NvidiaNvmlGpuTelemetryProvider>(trace, std::move(adapter));
}
