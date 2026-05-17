#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/strings.h"
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

template <typename Function> bool LoadFunction(HMODULE module, const char* name, Function& function) {
    function = reinterpret_cast<Function>(GetProcAddress(module, name));
    return function != nullptr;
}

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
            diagnostics = "NVML library not found.";
            return false;
        }

        bool loaded = true;
        loaded = LoadFunction(module_, "nvmlInit_v2", init_) && loaded;
        loaded = LoadFunction(module_, "nvmlShutdown", shutdown_) && loaded;
        loaded = LoadFunction(module_, "nvmlErrorString", errorString_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetCount_v2", deviceGetCount_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetHandleByIndex_v2", deviceGetHandleByIndex_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetName", deviceGetName_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetUtilizationRates", deviceGetUtilizationRates_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetTemperature", deviceGetTemperature_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetClockInfo", deviceGetClockInfo_) && loaded;
        loaded = LoadFunction(module_, "nvmlDeviceGetMemoryInfo", deviceGetMemoryInfo_) && loaded;
        LoadFunction(module_, "nvmlDeviceGetFanSpeedRPM", deviceGetFanSpeedRpm_);
        LoadFunction(module_, "nvmlDeviceGetPciInfo_v3", deviceGetPciInfo_);
        if (deviceGetPciInfo_ == nullptr) {
            LoadFunction(module_, "nvmlDeviceGetPciInfo_v2", deviceGetPciInfo_);
        }
        if (deviceGetPciInfo_ == nullptr) {
            LoadFunction(module_, "nvmlDeviceGetPciInfo", deviceGetPciInfo_);
        }

        if (!loaded) {
            diagnostics = "NVML library is missing required entry points.";
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
        return std::to_string(result);
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

bool NvmlPackedDeviceIdMatches(unsigned int pciDeviceId, const GpuVendorInfo& adapter) {
    const unsigned int low = pciDeviceId & 0xffffu;
    const unsigned int high = (pciDeviceId >> 16) & 0xffffu;
    return (low == adapter.vendorId && high == adapter.deviceId) ||
           (low == adapter.deviceId && high == adapter.vendorId);
}

int NvidiaDeviceMatchRank(const GpuVendorInfo& adapter, const NvmlPciInfo* pci, const std::string& name) {
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
    NvidiaNvmlGpuTelemetryProvider(Trace& trace, std::optional<GpuVendorInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    bool Initialize() override {
        trace_.Write(TracePrefix::NvidiaNvml, "initialize_begin");
        if (!nvml_.Load(diagnostics_)) {
            trace_.Write(TracePrefix::NvidiaNvml, "load_failed diagnostics=\"" + diagnostics_ + "\"");
            return false;
        }

        NvmlReturn result = nvml_.Initialize();
        trace_.Write(TracePrefix::NvidiaNvml, "init_done result=\"" + nvml_.ResultText(result) + "\"");
        if (result != kNvmlSuccess) {
            diagnostics_ = "NVML initialization failed: " + nvml_.ResultText(result);
            return false;
        }

        unsigned int deviceCount = 0;
        result = nvml_.DeviceCount(deviceCount);
        trace_.Write(TracePrefix::NvidiaNvml,
            "get_count result=\"" + nvml_.ResultText(result) + "\" count=" + std::to_string(deviceCount));
        if (result != kNvmlSuccess || deviceCount == 0) {
            diagnostics_ = "NVML found no NVIDIA GPUs: count=" + nvml_.ResultText(result);
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
        trace_.Write(TracePrefix::NvidiaNvml,
            "get_total_vram result=\"" + nvml_.ResultText(memoryResult) +
                "\" total_bytes=" + std::to_string(memory.total));
        if (memoryResult == kNvmlSuccess && memory.total > 0) {
            totalVramGb_ = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
        }

        diagnostics_ = "NVML GPU=" + gpuName_ + " fan_rpm_supported=" + (HasFanSpeedRpm() ? "yes" : "no") +
                       " native_fps_supported=no";
        fpsProvider_ = CreatePresentedFpsProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = "Presented FPS ETW provider active.";
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ =
                fpsSample.diagnostics.empty() ? "Presented FPS ETW provider unavailable." : fpsSample.diagnostics;
        }
        initialized_ = true;
        trace_.Write(TracePrefix::NvidiaNvml,
            "initialize_done diagnostics=\"" + diagnostics_ + "\" fps=\"" + fpsDiagnostics_ + "\"");
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace_.Write(TracePrefix::NvidiaNvml, "sample_begin");
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
        trace_.WriteLazy(TracePrefix::NvidiaNvml, [&] {
            return "get_utilization result=\"" + nvml_.ResultText(result) + "\" gpu=" + std::to_string(utilization.gpu);
        });
        if (result == kNvmlSuccess) {
            sample.loadPercent = static_cast<double>(utilization.gpu);
            hasAnyMetric = true;
        }

        unsigned int temperatureC = 0;
        result = nvml_.Temperature(device_, temperatureC);
        trace_.WriteLazy(TracePrefix::NvidiaNvml, [&] {
            return "get_temperature result=\"" + nvml_.ResultText(result) + "\" value=" + std::to_string(temperatureC);
        });
        if (result == kNvmlSuccess) {
            sample.temperatureC = static_cast<double>(temperatureC);
            hasAnyMetric = true;
        }

        unsigned int clockMhz = 0;
        result = nvml_.GraphicsClock(device_, clockMhz);
        trace_.WriteLazy(TracePrefix::NvidiaNvml,
            [&] { return "get_clock result=\"" + nvml_.ResultText(result) + "\" value=" + std::to_string(clockMhz); });
        if (result == kNvmlSuccess) {
            sample.coreClockMhz = static_cast<double>(clockMhz);
            hasAnyMetric = true;
        }

        NvmlMemory memory{};
        result = nvml_.MemoryInfo(device_, memory);
        trace_.WriteLazy(TracePrefix::NvidiaNvml, [&] {
            return "get_memory result=\"" + nvml_.ResultText(result) + "\" used_bytes=" + std::to_string(memory.used) +
                   " total_bytes=" + std::to_string(memory.total);
        });
        if (result == kNvmlSuccess) {
            sample.usedVramGb = static_cast<double>(memory.used) / (1024.0 * 1024.0 * 1024.0);
            if (memory.total > 0) {
                sample.totalVramGb = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
            }
            hasAnyMetric = true;
        }

        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> fanResult = nvml_.FanSpeedRpm(device_, fanRpm);
        trace_.WriteLazy(TracePrefix::NvidiaNvml, [&] {
            return fanResult.has_value()
                       ? "get_fan_rpm result=\"" + nvml_.ResultText(*fanResult) + "\" value=" + std::to_string(fanRpm)
                       : std::string("get_fan_rpm unavailable");
        });
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
            trace_.WriteLazy(TracePrefix::NvidiaNvml, [&] {
                return std::string("get_presented_fps available=") + Trace::BoolText(fpsSample.fps.has_value()) +
                       " value=" +
                       (fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1)
                                                  : std::string("fps=N/A")) +
                       " process=\"" + fpsSample.processName + "\" diagnostics=\"" + fpsSample.diagnostics + "\"";
            });
        }

        sample.available = hasAnyMetric;
        sample.diagnostics += " fps=" + fpsDiagnostics_;
        trace_.WriteLazy(TracePrefix::NvidiaNvml, [&] {
            return std::string("sample_done available=") + Trace::BoolText(sample.available) + " diagnostics=\"" +
                   sample.diagnostics + "\"";
        });
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
            trace_.Write(TracePrefix::NvidiaNvml,
                "device_candidate index=" + std::to_string(index) + " handle_result=\"" +
                    nvml_.ResultText(handleResult) + "\" name_result=\"" + nvml_.ResultText(nameResult) +
                    "\" pci_result=\"" +
                    (pciResult.has_value() ? nvml_.ResultText(*pciResult) : std::string("unavailable")) +
                    "\" pci=" + std::to_string(pci.domain) + ":" + std::to_string(pci.bus) + ":" +
                    std::to_string(pci.device) + " pci_device_id=0x" + HexId(pci.pciDeviceId, 8) + " subsystem_id=0x" +
                    HexId(pci.pciSubSystemId, 8) + " match_rank=" + std::to_string(rank) + " name=\"" + candidateName +
                    "\"");
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
        trace_.Write(TracePrefix::NvidiaNvml,
            "device_selected match=\"" + bestMatch + "\" rank=" + std::to_string(bestRank) + " display_name=\"" +
                gpuName_ + "\" selected_adapter=\"" + (adapter_.has_value() ? adapter_->adapterName : std::string()) +
                "\"");
        if (device_ == nullptr) {
            diagnostics_ = "NVML failed to open selected NVIDIA GPU: device=" + nvml_.ResultText(bestResult);
            return false;
        }
        return true;
    }

    static std::string HexId(unsigned int value, int width) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%0*X", width, value);
        return buffer;
    }

    bool HasFanSpeedRpm() const {
        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> result = device_ != nullptr ? nvml_.FanSpeedRpm(device_, fanRpm) : std::nullopt;
        return result.has_value() && *result == kNvmlSuccess;
    }

    Trace& trace_;
    NvmlLibrary nvml_;
    NvmlDevice device_ = nullptr;
    std::optional<GpuVendorInfo> adapter_;
    std::string gpuName_;
    std::string diagnostics_ = "NVML provider not initialized.";
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(
    Trace& trace, std::optional<GpuVendorInfo> adapter) {
    return std::make_unique<NvidiaNvmlGpuTelemetryProvider>(trace, std::move(adapter));
}
