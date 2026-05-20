#include "telemetry/gpu/amd/gpu_amd_adl.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "vendor/adlx/SDK/Include/IPerformanceMonitoring.h"
#include "vendor/adlx/SDK/Include/ISystem.h"

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_format.h"
#include "util/trace.h"

namespace {

using namespace adlx;

void SetSupportDiagnostics(std::string& diagnostics,
    const std::string& gpuName,
    adlx_bool usageSupported,
    ADLX_RESULT usageResult,
    adlx_bool tempSupported,
    ADLX_RESULT tempResult,
    adlx_bool clockSupported,
    ADLX_RESULT clockResult,
    adlx_bool fanSupported,
    ADLX_RESULT fanResult,
    adlx_bool vramSupported,
    ADLX_RESULT vramResult) {
    AssignFormat(diagnostics,
        RES_STR("ADLX GPU=%s usage_supported=%s(%d) temp_supported=%s(%d) clock_supported=%s(%d) fan_supported=%s(%d) "
                "vram_supported=%s(%d)"),
        gpuName.c_str(),
        usageSupported ? "yes" : "no",
        static_cast<int>(usageResult),
        tempSupported ? "yes" : "no",
        static_cast<int>(tempResult),
        clockSupported ? "yes" : "no",
        static_cast<int>(clockResult),
        fanSupported ? "yes" : "no",
        static_cast<int>(fanResult),
        vramSupported ? "yes" : "no",
        static_cast<int>(vramResult));
}

std::string AdlxString(const char* value) {
    return value != nullptr && value[0] != '\0' ? std::string(value) : std::string();
}

std::optional<unsigned int> ParseHexText(std::string text) {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    return end != nullptr && *end == '\0' ? std::optional<unsigned int>{static_cast<unsigned int>(value)}
                                          : std::nullopt;
}

std::optional<unsigned int> ParsePnpHexField(const std::string& text, const char* key) {
    const size_t pos = text.find(key);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const size_t start = pos + std::string(key).size();
    size_t end = start;
    while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || (text[end] >= 'a' && text[end] <= 'f') ||
                                    (text[end] >= 'A' && text[end] <= 'F'))) {
        ++end;
    }
    return ParseHexText(text.substr(start, end - start));
}

struct AdlxGpuIdentity {
    std::string name;
    std::string pnpString;
    unsigned int deviceId = 0;
    unsigned int vendorId = 0;
    unsigned int subSysId = 0;
    unsigned int revision = 0;
};

AdlxGpuIdentity ReadAdlxGpuIdentity(IADLXGPUPtr gpu) {
    AdlxGpuIdentity identity;
    const char* text = nullptr;
    if (gpu->Name(&text) == ADLX_OK) {
        identity.name = AdlxString(text);
    }
    if (gpu->PNPString(&text) == ADLX_OK) {
        identity.pnpString = AdlxString(text);
    }
    if (gpu->DeviceId(&text) == ADLX_OK) {
        identity.deviceId = ParseHexText(AdlxString(text)).value_or(0);
    }
    if (gpu->VendorId(&text) == ADLX_OK) {
        identity.vendorId = ParseHexText(AdlxString(text)).value_or(0);
    }

    if (!identity.pnpString.empty()) {
        identity.vendorId = ParsePnpHexField(identity.pnpString, "VEN_").value_or(identity.vendorId);
        identity.deviceId = ParsePnpHexField(identity.pnpString, "DEV_").value_or(identity.deviceId);
        identity.subSysId = ParsePnpHexField(identity.pnpString, "SUBSYS_").value_or(identity.subSysId);
        identity.revision = ParsePnpHexField(identity.pnpString, "REV_").value_or(identity.revision);
    }
    return identity;
}

int AmdDeviceMatchRank(const GpuAdapterInfo& adapter, const AdlxGpuIdentity& identity) {
    if (identity.vendorId == adapter.vendorId && identity.deviceId == adapter.deviceId &&
        (adapter.subSysId == 0 || identity.subSysId == adapter.subSysId) &&
        (adapter.revision == 0 || identity.revision == adapter.revision)) {
        return 4;
    }
    if (identity.vendorId == adapter.vendorId && identity.deviceId == adapter.deviceId) {
        return 3;
    }
    if (!adapter.adapterName.empty()) {
        if (EqualsInsensitive(identity.name, adapter.adapterName)) {
            return 2;
        }
        if (ContainsInsensitive(identity.name, adapter.adapterName) ||
            ContainsInsensitive(adapter.adapterName, identity.name)) {
            return 1;
        }
    }
    return 0;
}

class AmdAdlxGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    AmdAdlxGpuTelemetryProvider(Trace& trace, std::optional<GpuAdapterInfo> adapter)
        : trace_(trace), adapter_(std::move(adapter)) {}

    ~AmdAdlxGpuTelemetryProvider() override {
        metricsSupport_ = nullptr;
        performanceMonitoring_ = nullptr;
        gpu_ = nullptr;
        helper_.Terminate();
    }

    bool Initialize() override {
        trace().Write(TracePrefix::AmdAdlx, RES_STR("initialize_begin"));
        ADLX_RESULT result = helper_.Initialize();
        trace().WriteFmt(TracePrefix::AmdAdlx, RES_STR("helper_initialize result=%d"), static_cast<int>(result));
        if (ADLX_FAILED(result)) {
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("helper_initialize_incompatible_begin result=%d"),
                static_cast<int>(result));
            result = helper_.InitializeWithIncompatibleDriver();
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("helper_initialize_incompatible_done result=%d"),
                static_cast<int>(result));
        }
        if (ADLX_FAILED(result) || helper_.GetSystemServices() == nullptr) {
            diagnostics_ = FormatText(RES_STR("ADLX initialization failed: init=%d"), static_cast<int>(result));
            trace().WriteFmt(TracePrefix::AmdAdlx, RES_STR("initialize_failed %s"), diagnostics_.c_str());
            return false;
        }

        trace().Write(TracePrefix::AmdAdlx, RES_STR("get_performance_monitoring_begin"));
        result = helper_.GetSystemServices()->GetPerformanceMonitoringServices(&performanceMonitoring_);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_performance_monitoring_done result=%d available=%s"),
            static_cast<int>(result),
            Trace::BoolText(performanceMonitoring_ != nullptr));
        if (ADLX_FAILED(result) || !performanceMonitoring_) {
            diagnostics_ = FormatText(
                RES_STR("Failed to get ADLX performance monitoring services: perf=%d"), static_cast<int>(result));
            trace().WriteFmt(
                TracePrefix::AmdAdlx, RES_STR("get_performance_monitoring_failed %s"), diagnostics_.c_str());
            return false;
        }

        IADLXGPUListPtr gpus;
        trace().Write(TracePrefix::AmdAdlx, RES_STR("get_gpus_begin"));
        result = helper_.GetSystemServices()->GetGPUs(&gpus);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_gpus_done result=%d available=%s"),
            static_cast<int>(result),
            Trace::BoolText(gpus != nullptr));
        if (ADLX_FAILED(result) || !gpus || gpus->Empty()) {
            diagnostics_ = FormatText(RES_STR("Failed to get AMD GPU list: gpus=%d"), static_cast<int>(result));
            trace().WriteFmt(TracePrefix::AmdAdlx, RES_STR("get_gpus_failed %s"), diagnostics_.c_str());
            return false;
        }

        if (!SelectGpu(gpus)) {
            return false;
        }

        if (gpuName_.empty()) {
            gpuName_ = "AMD GPU";
        }

        adlx_uint totalVramMb = 0;
        const ADLX_RESULT totalVramResult = gpu_->TotalVRAM(&totalVramMb);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_total_vram result=%d mb=%u"),
            static_cast<int>(totalVramResult),
            static_cast<unsigned>(totalVramMb));
        if (ADLX_SUCCEEDED(totalVramResult) && totalVramMb > 0) {
            totalVramGb_ = static_cast<double>(totalVramMb) / 1024.0;
        }

        trace().Write(TracePrefix::AmdAdlx, RES_STR("get_supported_metrics_begin"));
        result = performanceMonitoring_->GetSupportedGPUMetrics(gpu_, &metricsSupport_);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_supported_metrics_done result=%d available=%s"),
            static_cast<int>(result),
            Trace::BoolText(metricsSupport_ != nullptr));
        if (ADLX_FAILED(result) || !metricsSupport_) {
            diagnostics_ =
                FormatText(RES_STR("Failed to query supported AMD GPU metrics: support=%d"), static_cast<int>(result));
            trace().WriteFmt(TracePrefix::AmdAdlx, RES_STR("get_supported_metrics_failed %s"), diagnostics_.c_str());
            return false;
        }

        adlx_bool tempSupported = false;
        adlx_bool usageSupported = false;
        adlx_bool clockSupported = false;
        adlx_bool fanSupported = false;
        adlx_bool vramSupported = false;
        const ADLX_RESULT usageResult = metricsSupport_->IsSupportedGPUUsage(&usageSupported);
        const ADLX_RESULT tempResult = metricsSupport_->IsSupportedGPUTemperature(&tempSupported);
        const ADLX_RESULT clockResult = metricsSupport_->IsSupportedGPUClockSpeed(&clockSupported);
        const ADLX_RESULT fanResult = metricsSupport_->IsSupportedGPUFanSpeed(&fanSupported);
        const ADLX_RESULT vramResult = metricsSupport_->IsSupportedGPUVRAM(&vramSupported);
        usageSupported_ = ADLX_SUCCEEDED(usageResult) && usageSupported;
        temperatureSupported_ = ADLX_SUCCEEDED(tempResult) && tempSupported;
        clockSupported_ = ADLX_SUCCEEDED(clockResult) && clockSupported;
        fanSupported_ = ADLX_SUCCEEDED(fanResult) && fanSupported;
        vramSupported_ = ADLX_SUCCEEDED(vramResult) && vramSupported;

        SetSupportDiagnostics(diagnostics_,
            gpuName_,
            usageSupported,
            usageResult,
            tempSupported,
            tempResult,
            clockSupported,
            clockResult,
            fanSupported,
            fanResult,
            vramSupported,
            vramResult);
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
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("initialize_done diagnostics=\"%s\" fps=\"%s\""),
            diagnostics_.c_str(),
            fpsDiagnostics_.c_str());
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace().Write(TracePrefix::AmdAdlx, RES_STR("sample_begin"));
        GpuVendorTelemetrySample sample;
        sample.providerName = "AMD ADLX";
        sample.name = gpuName_;
        sample.totalVramGb = totalVramGb_;
        sample.diagnostics = diagnostics_;

        if (!initialized_ || !performanceMonitoring_ || !gpu_ || !metricsSupport_) {
            sample.available = false;
            return sample;
        }

        IADLXGPUMetricsPtr metrics;
        trace().Write(TracePrefix::AmdAdlx, RES_STR("get_current_metrics_begin"));
        const ADLX_RESULT metricsResult = performanceMonitoring_->GetCurrentGPUMetrics(gpu_, &metrics);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_current_metrics_done result=%d available=%s"),
            static_cast<int>(metricsResult),
            Trace::BoolText(metrics != nullptr));
        if (ADLX_FAILED(metricsResult) || !metrics) {
            sample.diagnostics =
                FormatText(RES_STR("%s current_metrics=%d"), diagnostics_.c_str(), static_cast<int>(metricsResult));
            sample.available = false;
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("get_current_metrics_failed diagnostics=\"%s\""),
                sample.diagnostics.c_str());
            return sample;
        }
        bool hasAnyMetric = false;

        if (usageSupported_) {
            adlx_double usage = 0.0;
            trace().Write(TracePrefix::AmdAdlx, RES_STR("get_usage_begin"));
            const ADLX_RESULT result = metrics->GPUUsage(&usage);
            trace().WriteFmt(
                TracePrefix::AmdAdlx, RES_STR("get_usage_done result=%d value=%.1f"), static_cast<int>(result), usage);
            if (ADLX_SUCCEEDED(result)) {
                sample.loadPercent = usage;
                hasAnyMetric = true;
            }
        }

        if (temperatureSupported_) {
            adlx_double temperature = 0.0;
            trace().Write(TracePrefix::AmdAdlx, RES_STR("get_temperature_begin"));
            const ADLX_RESULT result = metrics->GPUTemperature(&temperature);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("get_temperature_done result=%d value=%.1f"),
                static_cast<int>(result),
                temperature);
            if (ADLX_SUCCEEDED(result)) {
                sample.temperatureC = temperature;
                hasAnyMetric = true;
            }
        }

        if (clockSupported_) {
            adlx_int clockMhz = 0;
            trace().Write(TracePrefix::AmdAdlx, RES_STR("get_clock_begin"));
            const ADLX_RESULT result = metrics->GPUClockSpeed(&clockMhz);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("get_clock_done result=%d value=%d"),
                static_cast<int>(result),
                static_cast<int>(clockMhz));
            if (ADLX_SUCCEEDED(result)) {
                sample.coreClockMhz = static_cast<double>(clockMhz);
                hasAnyMetric = true;
            }
        }

        if (fanSupported_) {
            adlx_int fanRpm = 0;
            trace().Write(TracePrefix::AmdAdlx, RES_STR("get_fan_begin"));
            const ADLX_RESULT result = metrics->GPUFanSpeed(&fanRpm);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("get_fan_done result=%d value=%d"),
                static_cast<int>(result),
                static_cast<int>(fanRpm));
            if (ADLX_SUCCEEDED(result)) {
                sample.fanRpm = static_cast<double>(fanRpm);
                hasAnyMetric = true;
            }
        }

        if (vramSupported_) {
            adlx_int usedVramMb = 0;
            trace().Write(TracePrefix::AmdAdlx, RES_STR("get_vram_begin"));
            const ADLX_RESULT result = metrics->GPUVRAM(&usedVramMb);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("get_vram_done result=%d value=%d"),
                static_cast<int>(result),
                static_cast<int>(usedVramMb));
            if (ADLX_SUCCEEDED(result) && usedVramMb >= 0) {
                sample.usedVramGb = static_cast<double>(usedVramMb) / 1024.0;
                hasAnyMetric = true;
            }
        }

        if (fpsProvider_ != nullptr) {
            const FpsTelemetrySample fpsSample = fpsProvider_->Sample();
            fpsDiagnostics_ = fpsSample.diagnostics;
            sample.fpsAppName = fpsSample.processName;
            sample.fpsPermissionRequired = fpsSample.permissionRequired;
            if (fpsSample.fps.has_value()) {
                sample.fps = *fpsSample.fps;
                hasAnyMetric = true;
            } else if (fpsSample.permissionRequired) {
                const std::optional<double> nativeFps = ReadNativeAmdFps();
                if (nativeFps.has_value()) {
                    sample.fps = *nativeFps;
                    sample.fpsAppName = "!admin";
                    hasAnyMetric = true;
                }
            }
            if (trace().Enabled(TracePrefix::AmdAdlx)) {
                const std::string fpsText =
                    fpsSample.fps.has_value() ? Trace::FormatValueDouble("fps", *fpsSample.fps, 1) : "fps=N/A";
                trace().WriteFmt(TracePrefix::AmdAdlx,
                    RES_STR("get_presented_fps available=%s permission_required=%s value=%s process=\"%s\" "
                            "diagnostics=\"%s\""),
                    Trace::BoolText(fpsSample.fps.has_value()),
                    Trace::BoolText(fpsSample.permissionRequired),
                    fpsText.c_str(),
                    fpsSample.processName.c_str(),
                    fpsSample.diagnostics.c_str());
            }
        }

        sample.available = hasAnyMetric;
        AppendFormat(sample.diagnostics, RES_STR(" fps=%s"), fpsDiagnostics_.c_str());
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("sample_done available=%s diagnostics=\"%s\""),
            Trace::BoolText(sample.available),
            sample.diagnostics.c_str());
        return sample;
    }

private:
    bool SelectGpu(const IADLXGPUListPtr& gpus) {
        int bestRank = -1;
        IADLXGPUPtr bestGpu;
        std::string bestName;
        std::string bestMatch = "fallback";
        ADLX_RESULT bestResult = ADLX_FAIL;

        for (adlx_uint index = gpus->Begin(); index < gpus->End(); ++index) {
            IADLXGPUPtr candidate;
            const ADLX_RESULT result = gpus->At(index, &candidate);
            const AdlxGpuIdentity identity = candidate != nullptr ? ReadAdlxGpuIdentity(candidate) : AdlxGpuIdentity{};
            const int rank = candidate != nullptr && adapter_.has_value() ? AmdDeviceMatchRank(*adapter_, identity)
                                                                          : (candidate != nullptr ? 0 : -1);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                RES_STR("gpu_candidate index=%u result=%d vendor_id=0x%04X device_id=0x%04X subsystem_id=0x%08X "
                        "revision=0x%02X match_rank=%d name=\"%s\" pnp=\"%s\""),
                static_cast<unsigned>(index),
                static_cast<int>(result),
                identity.vendorId,
                identity.deviceId,
                identity.subSysId,
                identity.revision,
                rank,
                identity.name.c_str(),
                identity.pnpString.c_str());
            if (candidate != nullptr && rank > bestRank) {
                bestRank = rank;
                bestGpu = candidate;
                bestName = identity.name;
                bestResult = result;
                bestMatch = rank >= 3 ? "device_id" : (rank >= 1 ? "name" : "fallback");
            }
        }

        gpu_ = bestGpu;
        gpuName_ = bestMatch == "device_id" && adapter_.has_value() && !adapter_->adapterName.empty()
                       ? adapter_->adapterName
                       : bestName;
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("gpu_selected match=\"%s\" rank=%d display_name=\"%s\" selected_adapter=\"%s\""),
            bestMatch.c_str(),
            bestRank,
            gpuName_.c_str(),
            adapter_.has_value() ? adapter_->adapterName.c_str() : "");
        if (!gpu_) {
            diagnostics_ = FormatText(RES_STR("Failed to open selected AMD GPU: gpu=%d"), static_cast<int>(bestResult));
            trace().WriteFmt(TracePrefix::AmdAdlx, RES_STR("get_gpu_failed %s"), diagnostics_.c_str());
            return false;
        }
        return true;
    }

    std::optional<double> ReadNativeAmdFps() {
        IADLXFPSPtr fpsMetric;
        trace().Write(TracePrefix::AmdAdlx, RES_STR("get_native_fps_begin"));
        const ADLX_RESULT fpsMetricResult = performanceMonitoring_->GetCurrentFPS(&fpsMetric);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_native_fps_metric_done result=%d available=%s"),
            static_cast<int>(fpsMetricResult),
            Trace::BoolText(fpsMetric != nullptr));
        if (ADLX_FAILED(fpsMetricResult) || !fpsMetric) {
            return std::nullopt;
        }

        adlx_int fps = 0;
        const ADLX_RESULT fpsResult = fpsMetric->FPS(&fps);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            RES_STR("get_native_fps_done result=%d value=%d"),
            static_cast<int>(fpsResult),
            static_cast<int>(fps));
        return ADLX_SUCCEEDED(fpsResult) && fps >= 0 ? std::optional<double>{static_cast<double>(fps)} : std::nullopt;
    }

    Trace& trace() {
        return trace_;
    }

    ADLXHelper helper_;
    IADLXGPUPtr gpu_;
    IADLXPerformanceMonitoringServicesPtr performanceMonitoring_;
    IADLXGPUMetricsSupportPtr metricsSupport_;
    Trace& trace_;
    std::optional<GpuAdapterInfo> adapter_;
    std::string gpuName_;
    std::string diagnostics_ = ResourceStringText(RES_STR("ADLX provider not initialized."));
    std::string fpsDiagnostics_ = ResourceStringText(RES_STR("Presented FPS ETW provider not initialized."));
    std::optional<double> totalVramGb_;
    std::unique_ptr<FpsTelemetryProvider> fpsProvider_;
    bool usageSupported_ = false;
    bool temperatureSupported_ = false;
    bool clockSupported_ = false;
    bool fanSupported_ = false;
    bool vramSupported_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(
    Trace& trace, std::optional<GpuAdapterInfo> adapter) {
    return std::make_unique<AmdAdlxGpuTelemetryProvider>(trace, std::move(adapter));
}
