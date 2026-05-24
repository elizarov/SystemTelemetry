#include "telemetry/gpu/nvidia/gpu_nvidia_nvml.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "telemetry/gpu/nvidia/impl/hdi_nvapi.h"
#include "telemetry/gpu/nvidia/impl/hdi_nvml.h"
#include "telemetry/impl/hdi.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

using NvmlDevice = HdiNvmlDevice;
using NvmlReturn = HdiNvmlReturn;
using NvmlMemory = HdiNvmlMemory;
using NvmlPciInfo = HdiNvmlPciInfo;
using NvApiStatus = HdiNvapiStatus;
using NvApiPhysicalGpuHandle = HdiNvapiPhysicalGpuHandle;

constexpr NvmlReturn kNvmlSuccess = kHdiNvmlSuccess;
constexpr NvApiStatus kNvApiOk = kHdiNvapiOk;
constexpr NvApiStatus kNvApiGpuNotPowered = kHdiNvapiGpuNotPowered;
constexpr int kNvApiShortStringSize = kHdiNvapiShortStringSize;

struct NvApiClockSample {
    NvApiStatus status = 0;
    std::optional<double> clockMhz;
};

std::string KnownNvmlName(const std::array<char, 128>& name) {
    return name[0] != '\0' ? std::string(name.data()) : std::string();
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
    NvidiaNvmlGpuTelemetryProvider(Trace& trace,
        std::optional<GpuAdapterInfo> adapter,
        bool collectPresentedFps,
        const HardwareDependencyInjection* injection)
        : trace_(trace), nvml_(ResolveHdiFactory(injection).CreateNvidiaNvmlHdi(trace)),
          nvapi_(ResolveHdiFactory(injection).CreateNvidiaNvapiHdi(trace)), adapter_(std::move(adapter)),
          collectPresentedFps_(collectPresentedFps) {}

    bool Initialize() override {
        trace_.Write(TracePrefix::NvidiaNvml, RES_STR("initialize_begin"));
        if (nvml_ == nullptr) {
            diagnostics_ = ResourceStringText(RES_STR("NVML library not found."));
            trace_.WriteFmt(TracePrefix::NvidiaNvml, RES_STR("load_failed diagnostics=\"%s\""), diagnostics_.c_str());
            return false;
        }
        if (!nvml_->Load(diagnostics_)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml, RES_STR("load_failed diagnostics=\"%s\""), diagnostics_.c_str());
            return false;
        }

        NvmlReturn result = nvml_->Initialize();
        trace_.WriteFmt(TracePrefix::NvidiaNvml, RES_STR("init_done result=\"%s\""), nvml_->ResultText(result).c_str());
        if (result != kNvmlSuccess) {
            diagnostics_ = FormatText(RES_STR("NVML initialization failed: %s"), nvml_->ResultText(result).c_str());
            return false;
        }

        unsigned int deviceCount = 0;
        result = nvml_->DeviceCount(deviceCount);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("get_count result=\"%s\" count=%u"),
            nvml_->ResultText(result).c_str(),
            deviceCount);
        if (result != kNvmlSuccess || deviceCount == 0) {
            diagnostics_ =
                FormatText(RES_STR("NVML found no NVIDIA GPUs: count=%s"), nvml_->ResultText(result).c_str());
            return false;
        }

        if (!SelectDevice(deviceCount)) {
            return false;
        }

        if (gpuName_.empty()) {
            gpuName_ = "NVIDIA GPU";
        }

        NvmlMemory memory{};
        const NvmlReturn memoryResult = nvml_->MemoryInfo(device_, memory);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("get_total_vram result=\"%s\" total_bytes=%llu"),
            nvml_->ResultText(memoryResult).c_str(),
            static_cast<unsigned long long>(memory.total));
        if (memoryResult == kNvmlSuccess && memory.total > 0) {
            totalVramGb_ = static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
        }

        fanRpmSupported_ = DetectFanSpeedRpm();
        nvapiClockAvailable_ = InitializeNvapiClock();
        diagnostics_ = FormatText(
            RES_STR("NVML GPU=%s load_source=pdh clock_source=%s fan_rpm_supported=%s native_fps_supported=no"),
            gpuName_.c_str(),
            nvapiClockAvailable_ ? "nvapi" : "unavailable",
            fanRpmSupported_ ? "yes" : "no");
        if (collectPresentedFps_) {
            fpsProvider_ = CreatePresentedFpsProvider(trace_, adapter_);
            if (fpsProvider_ != nullptr && fpsProvider_->Initialize()) {
                fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider active."));
            } else {
                const FpsTelemetrySample fpsSample =
                    fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
                fpsDiagnostics_ = fpsSample.diagnostics.empty()
                                      ? ResourceStringText(RES_STR("Presented FPS ETW provider unavailable."))
                                      : fpsSample.diagnostics;
            }
        } else {
            fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS collection not requested by layout."));
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
        NvmlReturn result = nvml_->Temperature(device_, temperatureC);
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("get_temperature result=\"%s\" value=%u"),
                nvml_->ResultText(result).c_str(),
                temperatureC);
        }
        if (result == kNvmlSuccess) {
            sample.temperatureC = static_cast<double>(temperatureC);
            hasAnyMetric = true;
        }

        if (nvapiClockAvailable_) {
            const NvApiClockSample clockSample = GraphicsClock();
            if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
                const double clockMhz = clockSample.clockMhz.value_or(0.0);
                trace_.WriteFmt(TracePrefix::NvidiaNvml,
                    RES_STR("get_clock_nvapi result=\"%s\" value_mhz=%.1f"),
                    nvapi_->ResultText(clockSample.status).c_str(),
                    clockMhz);
            }
            if (clockSample.clockMhz.has_value()) {
                sample.coreClockMhz = *clockSample.clockMhz;
                hasAnyMetric = true;
            }
        }

        NvmlMemory memory{};
        result = nvml_->MemoryInfo(device_, memory);
        if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("get_memory result=\"%s\" used_bytes=%llu total_bytes=%llu"),
                nvml_->ResultText(result).c_str(),
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
            fanRpmSupported_ ? nvml_->FanSpeedRpm(device_, fanRpm) : std::nullopt;
        if (fanRpmSupported_ && fanResult.has_value()) {
            if (trace_.Enabled(TracePrefix::NvidiaNvml)) {
                trace_.WriteFmt(TracePrefix::NvidiaNvml,
                    RES_STR("get_fan_rpm result=\"%s\" value=%u"),
                    nvml_->ResultText(*fanResult).c_str(),
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
            const NvmlReturn handleResult = nvml_->DeviceHandleByIndex(index, candidate);
            std::array<char, 128> name{};
            const NvmlReturn nameResult =
                candidate != nullptr ? nvml_->DeviceName(candidate, name.data(), static_cast<unsigned int>(name.size()))
                                     : handleResult;
            NvmlPciInfo pci{};
            const std::optional<NvmlReturn> pciResult =
                candidate != nullptr ? nvml_->PciInfo(candidate, pci) : std::nullopt;
            const bool pciOk = pciResult.has_value() && *pciResult == kNvmlSuccess;
            const std::string candidateName = nameResult == kNvmlSuccess ? KnownNvmlName(name) : std::string();
            const int rank = candidate != nullptr && adapter_.has_value()
                                 ? NvidiaDeviceMatchRank(*adapter_, pciOk ? &pci : nullptr, candidateName)
                                 : (candidate != nullptr ? 1 : 0);
            const std::string pciResultText =
                pciResult.has_value() ? nvml_->ResultText(*pciResult) : std::string("unavailable");
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("device_candidate index=%u handle_result=\"%s\" name_result=\"%s\" pci_result=\"%s\" "
                        "pci=%u:%u:%u pci_device_id=0x%08X subsystem_id=0x%08X match_rank=%d name=\"%s\""),
                index,
                nvml_->ResultText(handleResult).c_str(),
                nvml_->ResultText(nameResult).c_str(),
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
                RES_STR("NVML failed to open selected NVIDIA GPU: device=%s"), nvml_->ResultText(bestResult).c_str());
            return false;
        }
        return true;
    }

    bool InitializeNvapiClock() {
        if (nvapi_ == nullptr) {
            clockDiagnostics_ = ResourceStringText(RES_STR("NVAPI library not found."));
            return false;
        }
        if (!nvapi_->Load(clockDiagnostics_)) {
            return false;
        }

        const NvApiStatus initStatus = nvapi_->Initialize();
        trace_.WriteFmt(
            TracePrefix::NvidiaNvml, RES_STR("nvapi_init result=\"%s\""), nvapi_->ResultText(initStatus).c_str());
        if (initStatus != kNvApiOk) {
            clockDiagnostics_ =
                FormatText(RES_STR("NVAPI initialization failed: %s"), nvapi_->ResultText(initStatus).c_str());
            return false;
        }

        std::vector<NvApiPhysicalGpuHandle> handles;
        const NvApiStatus enumStatus = nvapi_->EnumPhysicalGpus(handles);
        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("nvapi_enum result=\"%s\" count=%d"),
            nvapi_->ResultText(enumStatus).c_str(),
            static_cast<int>(handles.size()));
        if (enumStatus != kNvApiOk || handles.empty()) {
            clockDiagnostics_ =
                FormatText(RES_STR("NVAPI found no NVIDIA GPUs: %s"), nvapi_->ResultText(enumStatus).c_str());
            return false;
        }

        int bestRank = -1;
        for (size_t index = 0; index < handles.size(); ++index) {
            if (handles[index] == nullptr) {
                continue;
            }
            char name[kNvApiShortStringSize] = {};
            const NvApiStatus nameStatus =
                nvapi_->GpuFullName(handles[index], name, static_cast<unsigned int>(sizeof(name)));
            const std::string candidateName = nameStatus == kNvApiOk ? std::string(name) : std::string();
            const int rank = !gpuName_.empty() && EqualsInsensitive(candidateName, gpuName_)
                                 ? 3
                                 : (!gpuName_.empty() && (ContainsInsensitive(candidateName, gpuName_) ||
                                                             ContainsInsensitive(gpuName_, candidateName))
                                           ? 2
                                           : 1);
            trace_.WriteFmt(TracePrefix::NvidiaNvml,
                RES_STR("nvapi_device_candidate index=%u name_result=\"%s\" match_rank=%d name=\"%s\""),
                static_cast<unsigned int>(index),
                nvapi_->ResultText(nameStatus).c_str(),
                rank,
                candidateName.c_str());
            if (nameStatus == kNvApiOk && rank > bestRank) {
                bestRank = rank;
                nvapiPhysicalGpu_ = handles[index];
                nvapiGpuName_ = candidateName;
            }
        }

        if (nvapiPhysicalGpu_ == nullptr) {
            clockDiagnostics_ = ResourceStringText(RES_STR("NVAPI failed to select a physical GPU."));
            return false;
        }

        trace_.WriteFmt(TracePrefix::NvidiaNvml,
            RES_STR("nvapi_device_selected rank=%d name=\"%s\""),
            bestRank,
            nvapiGpuName_.c_str());
        clockDiagnostics_ = FormatText(RES_STR("NVAPI clock GPU=%s"), nvapiGpuName_.c_str());
        return true;
    }

    NvApiClockSample GraphicsClock() const {
        NvApiClockSample sample;
        if (nvapi_ == nullptr || nvapiPhysicalGpu_ == nullptr) {
            sample.status = -1;
            return sample;
        }

        const HdiNvapiClockFrequencies frequencies = nvapi_->GraphicsClock(nvapiPhysicalGpu_);
        sample.status = frequencies.status;
        if (sample.status == kNvApiGpuNotPowered) {
            sample.clockMhz = 0.0;
            return sample;
        }
        if (sample.status == kNvApiOk && frequencies.graphicsPresent) {
            sample.clockMhz = static_cast<double>(frequencies.graphicsFrequencyKhz) / 1000.0;
        }
        return sample;
    }

    bool DetectFanSpeedRpm() const {
        unsigned int fanRpm = 0;
        const std::optional<NvmlReturn> result =
            device_ != nullptr ? nvml_->FanSpeedRpm(device_, fanRpm) : std::nullopt;
        return result.has_value() && *result == kNvmlSuccess;
    }

    Trace& trace_;
    std::unique_ptr<NvidiaNvmlHdi> nvml_;
    std::unique_ptr<NvidiaNvapiHdi> nvapi_;
    NvmlDevice device_ = nullptr;
    NvApiPhysicalGpuHandle nvapiPhysicalGpu_ = nullptr;
    std::optional<GpuAdapterInfo> adapter_;
    std::string gpuName_;
    std::string nvapiGpuName_;
    std::string diagnostics_ = ResourceStringText(RES_STR("NVML provider not initialized."));
    std::string clockDiagnostics_ = ResourceStringText(RES_STR("NVAPI clock provider not initialized."));
    std::string fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider not initialized."));
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool nvapiClockAvailable_ = false;
    bool fanRpmSupported_ = false;
    bool collectPresentedFps_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(
    Trace& trace, std::optional<GpuAdapterInfo> adapter, bool collectPresentedFps) {
    return CreateNvidiaGpuTelemetryProvider(trace, std::move(adapter), collectPresentedFps, nullptr);
}

std::unique_ptr<GpuVendorTelemetryProvider> CreateNvidiaGpuTelemetryProvider(Trace& trace,
    std::optional<GpuAdapterInfo> adapter,
    bool collectPresentedFps,
    const HardwareDependencyInjection* injection) {
    return std::make_unique<NvidiaNvmlGpuTelemetryProvider>(trace, std::move(adapter), collectPresentedFps, injection);
}
