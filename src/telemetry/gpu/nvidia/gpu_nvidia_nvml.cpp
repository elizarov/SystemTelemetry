#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"

#include <windows.h>

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "telemetry/fps/fps_etw_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using NvmlDevice = void*;
using NvmlReturn = int;

constexpr NvmlReturn kNvmlSuccess = 0;
constexpr unsigned int kNvmlTemperatureGpu = 0;
constexpr unsigned int kNvmlClockGraphics = 0;

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
        module_ = LoadLibraryW(L"nvml.dll");
        if (module_ == nullptr) {
            module_ = LoadLibraryW(L"nvidia-ml.dll");
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
    bool initialized_ = false;
};

class NvidiaNvmlGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    explicit NvidiaNvmlGpuTelemetryProvider(Trace& trace) : trace_(trace) {}

    bool Initialize() override {
        trace_.Write("nvidia_nvml:initialize_begin");
        if (!nvml_.Load(diagnostics_)) {
            trace_.Write("nvidia_nvml:load_failed diagnostics=\"" + diagnostics_ + "\"");
            return false;
        }

        NvmlReturn result = nvml_.Initialize();
        trace_.Write("nvidia_nvml:init_done result=\"" + nvml_.ResultText(result) + "\"");
        if (result != kNvmlSuccess) {
            diagnostics_ = "NVML initialization failed: " + nvml_.ResultText(result);
            return false;
        }

        unsigned int deviceCount = 0;
        result = nvml_.DeviceCount(deviceCount);
        trace_.Write(
            "nvidia_nvml:get_count result=\"" + nvml_.ResultText(result) + "\" count=" + std::to_string(deviceCount));
        if (result != kNvmlSuccess || deviceCount == 0) {
            diagnostics_ = "NVML found no NVIDIA GPUs: count=" + nvml_.ResultText(result);
            return false;
        }

        result = nvml_.DeviceHandleByIndex(0, device_);
        trace_.Write("nvidia_nvml:get_device result=\"" + nvml_.ResultText(result) +
                     "\" available=" + Trace::BoolText(device_ != nullptr));
        if (result != kNvmlSuccess || device_ == nullptr) {
            diagnostics_ = "NVML failed to open first NVIDIA GPU: device=" + nvml_.ResultText(result);
            return false;
        }

        std::array<char, 128> name{};
        const NvmlReturn nameResult = nvml_.DeviceName(device_, name.data(), static_cast<unsigned int>(name.size()));
        trace_.Write("nvidia_nvml:get_name result=\"" + nvml_.ResultText(nameResult) +
                     "\" has_name=" + Trace::BoolText(name[0] != '\0'));
        if (nameResult == kNvmlSuccess && name[0] != '\0') {
            gpuName_ = Utf8FromAnsi(name.data());
        }
        if (gpuName_.empty()) {
            gpuName_ = "NVIDIA GPU";
        }

        NvmlMemory memory{};
        const NvmlReturn memoryResult = nvml_.MemoryInfo(device_, memory);
        trace_.Write("nvidia_nvml:get_total_vram result=\"" + nvml_.ResultText(memoryResult) +
                     "\" total_bytes=" + std::to_string(memory.total));
        if (memoryResult == kNvmlSuccess && memory.total > 0) {
            totalVramGb_ = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
        }

        diagnostics_ = "NVML GPU=" + gpuName_ + " fan_rpm_supported=" + (HasFanSpeedRpm() ? "yes" : "no") +
                       " native_fps_supported=no";
        fpsProvider_ = CreatePresentedFpsEtwProvider(trace_);
        if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
            fpsDiagnostics_ = "Presented FPS ETW provider active.";
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ =
                fpsSample.diagnostics.empty() ? "Presented FPS ETW provider unavailable." : fpsSample.diagnostics;
        }
        initialized_ = true;
        trace_.Write(
            "nvidia_nvml:initialize_done diagnostics=\"" + diagnostics_ + "\" fps=\"" + fpsDiagnostics_ + "\"");
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace_.Write("nvidia_nvml:sample_begin");
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
        trace_.WriteLazy([&] {
            return "nvidia_nvml:get_utilization result=\"" + nvml_.ResultText(result) +
                   "\" gpu=" + std::to_string(utilization.gpu);
        });
        if (result == kNvmlSuccess) {
            sample.loadPercent = static_cast<double>(utilization.gpu);
            hasAnyMetric = true;
        }

        unsigned int temperatureC = 0;
        result = nvml_.Temperature(device_, temperatureC);
        trace_.WriteLazy([&] {
            return "nvidia_nvml:get_temperature result=\"" + nvml_.ResultText(result) +
                   "\" value=" + std::to_string(temperatureC);
        });
        if (result == kNvmlSuccess) {
            sample.temperatureC = static_cast<double>(temperatureC);
            hasAnyMetric = true;
        }

        unsigned int clockMhz = 0;
        result = nvml_.GraphicsClock(device_, clockMhz);
        trace_.WriteLazy([&] {
            return "nvidia_nvml:get_clock result=\"" + nvml_.ResultText(result) +
                   "\" value=" + std::to_string(clockMhz);
        });
        if (result == kNvmlSuccess) {
            sample.coreClockMhz = static_cast<double>(clockMhz);
            hasAnyMetric = true;
        }

        NvmlMemory memory{};
        result = nvml_.MemoryInfo(device_, memory);
        trace_.WriteLazy([&] {
            return "nvidia_nvml:get_memory result=\"" + nvml_.ResultText(result) +
                   "\" used_bytes=" + std::to_string(memory.used) + " total_bytes=" + std::to_string(memory.total);
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
        trace_.WriteLazy([&] {
            return fanResult.has_value() ? "nvidia_nvml:get_fan_rpm result=\"" + nvml_.ResultText(*fanResult) +
                                               "\" value=" + std::to_string(fanRpm)
                                         : std::string("nvidia_nvml:get_fan_rpm unavailable");
        });
        if (fanResult.has_value() && *fanResult == kNvmlSuccess) {
            sample.fanRpm = static_cast<double>(fanRpm);
            hasAnyMetric = true;
        }

        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                hasAnyMetric = true;
            }
            trace_.WriteLazy([&] {
                return "nvidia_nvml:get_presented_fps available=" + Trace::BoolText(fpsSample.fps.has_value()) +
                       " value=" +
                       (fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1)
                                                  : std::string("fps=N/A")) +
                       " process=\"" + fpsSample.processName + "\" diagnostics=\"" + fpsSample.diagnostics + "\"";
            });
        }

        sample.available = hasAnyMetric;
        sample.diagnostics += " fps=" + fpsDiagnostics_;
        trace_.WriteLazy([&] {
            return "nvidia_nvml:sample_done available=" + Trace::BoolText(sample.available) + " diagnostics=\"" +
                   sample.diagnostics + "\"";
        });
        return sample;
    }

private:
    bool HasFanSpeedRpm() const {
        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> result = device_ != nullptr ? nvml_.FanSpeedRpm(device_, fanRpm) : std::nullopt;
        return result.has_value() && *result == kNvmlSuccess;
    }

    Trace& trace_;
    NvmlLibrary nvml_;
    NvmlDevice device_ = nullptr;
    std::string gpuName_;
    std::string diagnostics_ = "NVML provider not initialized.";
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(Trace& trace) {
    return std::make_unique<NvidiaNvmlGpuTelemetryProvider>(trace);
}
