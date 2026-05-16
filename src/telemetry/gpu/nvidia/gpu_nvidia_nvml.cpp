#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"

#include <windows.h>

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
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
        trace_.Write(TracePrefix::NvidiaNvml, "initialize_begin");
        if (!nvml_.Load(diagnostics_)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml, "load_failed diagnostics=\"%s\"", diagnostics_.c_str());
            return false;
        }

        NvmlReturn result = nvml_.Initialize();
        trace_.WriteFmt(TracePrefix::NvidiaNvml, "init_done result=\"%s\"", nvml_.ResultText(result).c_str());
        if (result != kNvmlSuccess) {
            diagnostics_ = FormatText("NVML initialization failed: %s", nvml_.ResultText(result).c_str());
            return false;
        }

        unsigned int deviceCount = 0;
        result = nvml_.DeviceCount(deviceCount);
        trace_.WriteFmt(
            TracePrefix::NvidiaNvml, "get_count result=\"%s\" count=%u", nvml_.ResultText(result).c_str(), deviceCount);
        if (result != kNvmlSuccess || deviceCount == 0) {
            diagnostics_ = FormatText("NVML found no NVIDIA GPUs: count=%s", nvml_.ResultText(result).c_str());
            return false;
        }

        result = nvml_.DeviceHandleByIndex(0, device_);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            "get_device result=\"%s\" available=%s",
            nvml_.ResultText(result).c_str(),
            Trace::BoolText(device_ != nullptr));
        if (result != kNvmlSuccess || device_ == nullptr) {
            diagnostics_ =
                FormatText("NVML failed to open first NVIDIA GPU: device=%s", nvml_.ResultText(result).c_str());
            return false;
        }

        std::array<char, 128> name{};
        const NvmlReturn nameResult = nvml_.DeviceName(device_, name.data(), static_cast<unsigned int>(name.size()));
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            "get_name result=\"%s\" has_name=%s",
            nvml_.ResultText(nameResult).c_str(),
            Trace::BoolText(name[0] != '\0'));
        if (nameResult == kNvmlSuccess && name[0] != '\0') {
            gpuName_ = Utf8FromAnsi(name.data());
        }
        if (gpuName_.empty()) {
            gpuName_ = "NVIDIA GPU";
        }

        NvmlMemory memory{};
        const NvmlReturn memoryResult = nvml_.MemoryInfo(device_, memory);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            "get_total_vram result=\"%s\" total_bytes=%llu",
            nvml_.ResultText(memoryResult).c_str(),
            static_cast<unsigned long long>(memory.total));
        if (memoryResult == kNvmlSuccess && memory.total > 0) {
            totalVramGb_ = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
        }

        diagnostics_ = FormatText("NVML GPU=%s fan_rpm_supported=%s native_fps_supported=no",
            gpuName_.c_str(),
            HasFanSpeedRpm() ? "yes" : "no");
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
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            "initialize_done diagnostics=\"%s\" fps=\"%s\"",
            diagnostics_.c_str(),
            fpsDiagnostics_.c_str());
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
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                "get_utilization result=\"%s\" gpu=%u",
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
                "get_temperature result=\"%s\" value=%u",
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
                "get_clock result=\"%s\" value=%u",
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
                "get_memory result=\"%s\" used_bytes=%llu total_bytes=%llu",
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
                    "get_fan_rpm result=\"%s\" value=%u",
                    nvml_.ResultText(*fanResult).c_str(),
                    fanRpm);
            }
        } else {
            trace_.Write(TracePrefix::NvidiaNvml, "get_fan_rpm unavailable");
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
                    "get_presented_fps available=%s value=%s process=\"%s\" diagnostics=\"%s\"",
                    Trace::BoolText(fpsSample.fps.has_value()),
                    fpsText.c_str(),
                    fpsSample.processName.c_str(),
                    fpsSample.diagnostics.c_str());
            }
        }

        sample.available = hasAnyMetric;
        AppendFormat(sample.diagnostics, " fps=%s", fpsDiagnostics_.c_str());
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            "sample_done available=%s diagnostics=\"%s\"",
            Trace::BoolText(sample.available),
            sample.diagnostics.c_str());
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
