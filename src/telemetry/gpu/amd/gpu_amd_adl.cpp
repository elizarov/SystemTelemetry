#include "telemetry/gpu/amd/gpu_amd_adl.h"

#include <memory>
#include <optional>
#include <string>

#include "vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "vendor/adlx/SDK/Include/IPerformanceMonitoring.h"

#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor.h"
#include "util/text_format.h"
#include "util/trace.h"
#include "util/utf8.h"

namespace {

using namespace adlx;

std::string AdlxResultCodeString(ADLX_RESULT result) {
    return FormatText("%d", static_cast<int>(result));
}

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
    diagnostics = "ADLX GPU=";
    diagnostics += gpuName;
    AppendFormat(diagnostics,
        " usage_supported=%s(%d) temp_supported=%s(%d) clock_supported=%s(%d) fan_supported=%s(%d) "
        "vram_supported=%s(%d)",
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

class AmdAdlxGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    explicit AmdAdlxGpuTelemetryProvider(Trace& trace) : trace_(trace) {}

    ~AmdAdlxGpuTelemetryProvider() override {
        metricsSupport_ = nullptr;
        performanceMonitoring_ = nullptr;
        gpu_ = nullptr;
        helper_.Terminate();
    }

    bool Initialize() override {
        trace().Write(TracePrefix::AmdAdlx, "initialize_begin");
        ADLX_RESULT result = helper_.Initialize();
        trace().WriteFmt(TracePrefix::AmdAdlx, "helper_initialize result=%d", static_cast<int>(result));
        if (ADLX_FAILED(result)) {
            trace().WriteFmt(
                TracePrefix::AmdAdlx, "helper_initialize_incompatible_begin result=%d", static_cast<int>(result));
            result = helper_.InitializeWithIncompatibleDriver();
            trace().WriteFmt(
                TracePrefix::AmdAdlx, "helper_initialize_incompatible_done result=%d", static_cast<int>(result));
        }
        if (ADLX_FAILED(result) || helper_.GetSystemServices() == nullptr) {
            diagnostics_ = "ADLX initialization failed: init=" + AdlxResultCodeString(result);
            trace().WriteFmt(TracePrefix::AmdAdlx, "initialize_failed %s", diagnostics_.c_str());
            return false;
        }

        trace().Write(TracePrefix::AmdAdlx, "get_performance_monitoring_begin");
        result = helper_.GetSystemServices()->GetPerformanceMonitoringServices(&performanceMonitoring_);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_performance_monitoring_done result=%d available=%s",
            static_cast<int>(result),
            Trace::BoolText(performanceMonitoring_ != nullptr));
        if (ADLX_FAILED(result) || !performanceMonitoring_) {
            diagnostics_ = "Failed to get ADLX performance monitoring services: perf=" + AdlxResultCodeString(result);
            trace().WriteFmt(TracePrefix::AmdAdlx, "get_performance_monitoring_failed %s", diagnostics_.c_str());
            return false;
        }

        IADLXGPUListPtr gpus;
        trace().Write(TracePrefix::AmdAdlx, "get_gpus_begin");
        result = helper_.GetSystemServices()->GetGPUs(&gpus);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_gpus_done result=%d available=%s",
            static_cast<int>(result),
            Trace::BoolText(gpus != nullptr));
        if (ADLX_FAILED(result) || !gpus || gpus->Empty()) {
            diagnostics_ = "Failed to get AMD GPU list: gpus=" + AdlxResultCodeString(result);
            trace().WriteFmt(TracePrefix::AmdAdlx, "get_gpus_failed %s", diagnostics_.c_str());
            return false;
        }

        trace().Write(TracePrefix::AmdAdlx, "get_first_gpu_begin");
        result = gpus->At(gpus->Begin(), &gpu_);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_first_gpu_done result=%d available=%s",
            static_cast<int>(result),
            Trace::BoolText(gpu_ != nullptr));
        if (ADLX_FAILED(result) || !gpu_) {
            diagnostics_ = "Failed to open first AMD GPU: gpu=" + AdlxResultCodeString(result);
            trace().WriteFmt(TracePrefix::AmdAdlx, "get_first_gpu_failed %s", diagnostics_.c_str());
            return false;
        }

        const char* name = nullptr;
        const ADLX_RESULT nameResult = gpu_->Name(&name);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_gpu_name result=%d has_name=%s",
            static_cast<int>(nameResult),
            Trace::BoolText(name != nullptr && name[0] != '\0'));
        if (ADLX_SUCCEEDED(nameResult)) {
            gpuName_ = Utf8FromAnsi(name);
        }
        if (gpuName_.empty()) {
            gpuName_ = "AMD GPU";
        }

        adlx_uint totalVramMb = 0;
        const ADLX_RESULT totalVramResult = gpu_->TotalVRAM(&totalVramMb);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_total_vram result=%d mb=%u",
            static_cast<int>(totalVramResult),
            static_cast<unsigned>(totalVramMb));
        if (ADLX_SUCCEEDED(totalVramResult) && totalVramMb > 0) {
            totalVramGb_ = static_cast<double>(totalVramMb) / 1024.0;
        }

        trace().Write(TracePrefix::AmdAdlx, "get_supported_metrics_begin");
        result = performanceMonitoring_->GetSupportedGPUMetrics(gpu_, &metricsSupport_);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_supported_metrics_done result=%d available=%s",
            static_cast<int>(result),
            Trace::BoolText(metricsSupport_ != nullptr));
        if (ADLX_FAILED(result) || !metricsSupport_) {
            diagnostics_ = "Failed to query supported AMD GPU metrics: support=" + AdlxResultCodeString(result);
            trace().WriteFmt(TracePrefix::AmdAdlx, "get_supported_metrics_failed %s", diagnostics_.c_str());
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
            fpsDiagnostics_ = "Presented FPS ETW provider active.";
        } else {
            const FpsTelemetrySample fpsSample =
                fpsProvider_ != nullptr ? fpsProvider_->Sample() : FpsTelemetrySample{};
            fpsDiagnostics_ =
                fpsSample.diagnostics.empty() ? "Presented FPS ETW provider unavailable." : fpsSample.diagnostics;
        }
        initialized_ = true;
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "initialize_done diagnostics=\"%s\" fps=\"%s\"",
            diagnostics_.c_str(),
            fpsDiagnostics_.c_str());
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace().Write(TracePrefix::AmdAdlx, "sample_begin");
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
        trace().Write(TracePrefix::AmdAdlx, "get_current_metrics_begin");
        const ADLX_RESULT metricsResult = performanceMonitoring_->GetCurrentGPUMetrics(gpu_, &metrics);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_current_metrics_done result=%d available=%s",
            static_cast<int>(metricsResult),
            Trace::BoolText(metrics != nullptr));
        if (ADLX_FAILED(metricsResult) || !metrics) {
            sample.diagnostics = diagnostics_ + " current_metrics=" + AdlxResultCodeString(metricsResult);
            sample.available = false;
            trace().WriteFmt(
                TracePrefix::AmdAdlx, "get_current_metrics_failed diagnostics=\"%s\"", sample.diagnostics.c_str());
            return sample;
        }
        bool hasAnyMetric = false;

        if (usageSupported_) {
            adlx_double usage = 0.0;
            trace().Write(TracePrefix::AmdAdlx, "get_usage_begin");
            const ADLX_RESULT result = metrics->GPUUsage(&usage);
            trace().WriteFmt(
                TracePrefix::AmdAdlx, "get_usage_done result=%d value=%.1f", static_cast<int>(result), usage);
            if (ADLX_SUCCEEDED(result)) {
                sample.loadPercent = usage;
                hasAnyMetric = true;
            }
        }

        if (temperatureSupported_) {
            adlx_double temperature = 0.0;
            trace().Write(TracePrefix::AmdAdlx, "get_temperature_begin");
            const ADLX_RESULT result = metrics->GPUTemperature(&temperature);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                "get_temperature_done result=%d value=%.1f",
                static_cast<int>(result),
                temperature);
            if (ADLX_SUCCEEDED(result)) {
                sample.temperatureC = temperature;
                hasAnyMetric = true;
            }
        }

        if (clockSupported_) {
            adlx_int clockMhz = 0;
            trace().Write(TracePrefix::AmdAdlx, "get_clock_begin");
            const ADLX_RESULT result = metrics->GPUClockSpeed(&clockMhz);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                "get_clock_done result=%d value=%d",
                static_cast<int>(result),
                static_cast<int>(clockMhz));
            if (ADLX_SUCCEEDED(result)) {
                sample.coreClockMhz = static_cast<double>(clockMhz);
                hasAnyMetric = true;
            }
        }

        if (fanSupported_) {
            adlx_int fanRpm = 0;
            trace().Write(TracePrefix::AmdAdlx, "get_fan_begin");
            const ADLX_RESULT result = metrics->GPUFanSpeed(&fanRpm);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                "get_fan_done result=%d value=%d",
                static_cast<int>(result),
                static_cast<int>(fanRpm));
            if (ADLX_SUCCEEDED(result)) {
                sample.fanRpm = static_cast<double>(fanRpm);
                hasAnyMetric = true;
            }
        }

        if (vramSupported_) {
            adlx_int usedVramMb = 0;
            trace().Write(TracePrefix::AmdAdlx, "get_vram_begin");
            const ADLX_RESULT result = metrics->GPUVRAM(&usedVramMb);
            trace().WriteFmt(TracePrefix::AmdAdlx,
                "get_vram_done result=%d value=%d",
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
                    "get_presented_fps available=%s permission_required=%s value=%s process=\"%s\" diagnostics=\"%s\"",
                    Trace::BoolText(fpsSample.fps.has_value()),
                    Trace::BoolText(fpsSample.permissionRequired),
                    fpsText.c_str(),
                    fpsSample.processName.c_str(),
                    fpsSample.diagnostics.c_str());
            }
        }

        sample.available = hasAnyMetric;
        sample.diagnostics += " fps=" + fpsDiagnostics_;
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "sample_done available=%s diagnostics=\"%s\"",
            Trace::BoolText(sample.available),
            sample.diagnostics.c_str());
        return sample;
    }

private:
    std::optional<double> ReadNativeAmdFps() {
        IADLXFPSPtr fpsMetric;
        trace().Write(TracePrefix::AmdAdlx, "get_native_fps_begin");
        const ADLX_RESULT fpsMetricResult = performanceMonitoring_->GetCurrentFPS(&fpsMetric);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_native_fps_metric_done result=%d available=%s",
            static_cast<int>(fpsMetricResult),
            Trace::BoolText(fpsMetric != nullptr));
        if (ADLX_FAILED(fpsMetricResult) || !fpsMetric) {
            return std::nullopt;
        }

        adlx_int fps = 0;
        const ADLX_RESULT fpsResult = fpsMetric->FPS(&fps);
        trace().WriteFmt(TracePrefix::AmdAdlx,
            "get_native_fps_done result=%d value=%d",
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
    std::string gpuName_;
    std::string diagnostics_ = "ADLX provider not initialized.";
    std::string fpsDiagnostics_ = "Presented FPS ETW provider not initialized.";
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

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(Trace& trace) {
    return std::make_unique<AmdAdlxGpuTelemetryProvider>(trace);
}
