#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "vendor/adlx/SDK/Include/IPerformanceMonitoring.h"
#include "gpu_vendor.h"
#include "trace.h"
#include "utf8.h"

namespace {

using namespace adlx;

std::string Utf8FromAnsi(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    return Utf8FromCodePage(text, CP_ACP);
}

class AmdAdlxGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    explicit AmdAdlxGpuTelemetryProvider(tracing::Trace* trace) : trace_(trace) {}

    ~AmdAdlxGpuTelemetryProvider() override {
        metricsSupport_ = nullptr;
        performanceMonitoring_ = nullptr;
        gpu_ = nullptr;
        helper_.Terminate();
    }

    bool Initialize() override {
        trace().Write("amd_adlx:initialize_begin");
        ADLX_RESULT result = helper_.Initialize();
        trace().Write(
            "amd_adlx:helper_initialize " + tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)));
        if (ADLX_FAILED(result)) {
            trace().Write("amd_adlx:helper_initialize_incompatible_begin " +
                          tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)));
            result = helper_.InitializeWithIncompatibleDriver();
            trace().Write("amd_adlx:helper_initialize_incompatible_done " +
                          tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)));
        }
        if (ADLX_FAILED(result) || helper_.GetSystemServices() == nullptr) {
            diagnostics_ =
                "ADLX initialization failed: " + tracing::Trace::FormatAdlxResult("init", static_cast<int>(result));
            trace().Write("amd_adlx:initialize_failed " + diagnostics_);
            return false;
        }

        trace().Write("amd_adlx:get_performance_monitoring_begin");
        result = helper_.GetSystemServices()->GetPerformanceMonitoringServices(&performanceMonitoring_);
        trace().Write("amd_adlx:get_performance_monitoring_done " +
                      tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)) +
                      " available=" + tracing::Trace::BoolText(performanceMonitoring_ != nullptr));
        if (ADLX_FAILED(result) || !performanceMonitoring_) {
            diagnostics_ = "Failed to get ADLX performance monitoring services: " +
                           tracing::Trace::FormatAdlxResult("perf", static_cast<int>(result));
            trace().Write("amd_adlx:get_performance_monitoring_failed " + diagnostics_);
            return false;
        }

        IADLXGPUListPtr gpus;
        trace().Write("amd_adlx:get_gpus_begin");
        result = helper_.GetSystemServices()->GetGPUs(&gpus);
        trace().Write("amd_adlx:get_gpus_done " + tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)) +
                      " available=" + tracing::Trace::BoolText(gpus != nullptr));
        if (ADLX_FAILED(result) || !gpus || gpus->Empty()) {
            diagnostics_ =
                "Failed to get AMD GPU list: " + tracing::Trace::FormatAdlxResult("gpus", static_cast<int>(result));
            trace().Write("amd_adlx:get_gpus_failed " + diagnostics_);
            return false;
        }

        trace().Write("amd_adlx:get_first_gpu_begin");
        result = gpus->At(gpus->Begin(), &gpu_);
        trace().Write("amd_adlx:get_first_gpu_done " +
                      tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)) +
                      " available=" + tracing::Trace::BoolText(gpu_ != nullptr));
        if (ADLX_FAILED(result) || !gpu_) {
            diagnostics_ =
                "Failed to open first AMD GPU: " + tracing::Trace::FormatAdlxResult("gpu", static_cast<int>(result));
            trace().Write("amd_adlx:get_first_gpu_failed " + diagnostics_);
            return false;
        }

        const char* name = nullptr;
        const ADLX_RESULT nameResult = gpu_->Name(&name);
        trace().Write("amd_adlx:get_gpu_name " +
                      tracing::Trace::FormatAdlxResult("result", static_cast<int>(nameResult)) +
                      " has_name=" + tracing::Trace::BoolText(name != nullptr && name[0] != '\0'));
        if (ADLX_SUCCEEDED(nameResult)) {
            gpuName_ = Utf8FromAnsi(name);
        }
        if (gpuName_.empty()) {
            gpuName_ = "AMD GPU";
        }

        adlx_uint totalVramMb = 0;
        const ADLX_RESULT totalVramResult = gpu_->TotalVRAM(&totalVramMb);
        trace().Write("amd_adlx:get_total_vram " +
                      tracing::Trace::FormatAdlxResult("result", static_cast<int>(totalVramResult)) +
                      " mb=" + std::to_string(totalVramMb));
        if (ADLX_SUCCEEDED(totalVramResult) && totalVramMb > 0) {
            totalVramGb_ = static_cast<double>(totalVramMb) / 1024.0;
        }

        trace().Write("amd_adlx:get_supported_metrics_begin");
        result = performanceMonitoring_->GetSupportedGPUMetrics(gpu_, &metricsSupport_);
        trace().Write("amd_adlx:get_supported_metrics_done " +
                      tracing::Trace::FormatAdlxResult("result", static_cast<int>(result)) +
                      " available=" + tracing::Trace::BoolText(metricsSupport_ != nullptr));
        if (ADLX_FAILED(result) || !metricsSupport_) {
            diagnostics_ = "Failed to query supported AMD GPU metrics: " +
                           tracing::Trace::FormatAdlxResult("support", static_cast<int>(result));
            trace().Write("amd_adlx:get_supported_metrics_failed " + diagnostics_);
            return false;
        }

        adlx_bool tempSupported = false;
        adlx_bool clockSupported = false;
        adlx_bool fanSupported = false;
        const ADLX_RESULT tempResult = metricsSupport_->IsSupportedGPUTemperature(&tempSupported);
        const ADLX_RESULT clockResult = metricsSupport_->IsSupportedGPUClockSpeed(&clockSupported);
        const ADLX_RESULT fanResult = metricsSupport_->IsSupportedGPUFanSpeed(&fanSupported);
        temperatureSupported_ = ADLX_SUCCEEDED(tempResult) && tempSupported;
        clockSupported_ = ADLX_SUCCEEDED(clockResult) && clockSupported;
        fanSupported_ = ADLX_SUCCEEDED(fanResult) && fanSupported;

        std::ostringstream diag;
        diag << "ADLX GPU=" << gpuName_ << " temp_supported=" << (tempSupported ? "yes" : "no") << "("
             << static_cast<int>(tempResult) << ")"
             << " clock_supported=" << (clockSupported ? "yes" : "no") << "(" << static_cast<int>(clockResult) << ")"
             << " fan_supported=" << (fanSupported ? "yes" : "no") << "(" << static_cast<int>(fanResult) << ")";
        diagnostics_ = diag.str();
        initialized_ = true;
        trace().Write("amd_adlx:initialize_done diagnostics=\"" + diagnostics_ + "\"");
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        trace().Write("amd_adlx:sample_begin");
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
        trace().Write("amd_adlx:get_current_metrics_begin");
        const ADLX_RESULT metricsResult = performanceMonitoring_->GetCurrentGPUMetrics(gpu_, &metrics);
        trace().WriteLazy([&] {
            return "amd_adlx:get_current_metrics_done " +
                   tracing::Trace::FormatAdlxResult("result", static_cast<int>(metricsResult)) +
                   " available=" + tracing::Trace::BoolText(metrics != nullptr);
        });
        if (ADLX_FAILED(metricsResult) || !metrics) {
            sample.diagnostics = diagnostics_ + " " +
                                 tracing::Trace::FormatAdlxResult("current_metrics", static_cast<int>(metricsResult));
            sample.available = false;
            trace().WriteLazy([&] { return "amd_adlx:get_current_metrics_failed diagnostics=\"" + sample.diagnostics + "\""; });
            return sample;
        }
        bool hasAnyMetric = false;

        if (temperatureSupported_) {
            adlx_double temperature = 0.0;
            trace().Write("amd_adlx:get_temperature_begin");
            const ADLX_RESULT result = metrics->GPUTemperature(&temperature);
            {
                char buffer[128];
                sprintf_s(buffer,
                    "amd_adlx:get_temperature_done result=%d value=%.1f",
                    static_cast<int>(result),
                    temperature);
                trace().Write(buffer);
            }
            if (ADLX_SUCCEEDED(result)) {
                sample.temperatureC = temperature;
                hasAnyMetric = true;
            }
        }

        if (clockSupported_) {
            adlx_int clockMhz = 0;
            trace().Write("amd_adlx:get_clock_begin");
            const ADLX_RESULT result = metrics->GPUClockSpeed(&clockMhz);
            {
                char buffer[128];
                sprintf_s(buffer,
                    "amd_adlx:get_clock_done result=%d value=%d",
                    static_cast<int>(result),
                    static_cast<int>(clockMhz));
                trace().Write(buffer);
            }
            if (ADLX_SUCCEEDED(result)) {
                sample.coreClockMhz = static_cast<double>(clockMhz);
                hasAnyMetric = true;
            }
        }

        if (fanSupported_) {
            adlx_int fanRpm = 0;
            trace().Write("amd_adlx:get_fan_begin");
            const ADLX_RESULT result = metrics->GPUFanSpeed(&fanRpm);
            {
                char buffer[128];
                sprintf_s(buffer,
                    "amd_adlx:get_fan_done result=%d value=%d",
                    static_cast<int>(result),
                    static_cast<int>(fanRpm));
                trace().Write(buffer);
            }
            if (ADLX_SUCCEEDED(result)) {
                sample.fanRpm = static_cast<double>(fanRpm);
                hasAnyMetric = true;
            }
        }

        sample.available = hasAnyMetric;
        trace().WriteLazy([&] {
            return "amd_adlx:sample_done available=" + tracing::Trace::BoolText(sample.available) + " diagnostics=\"" +
                   sample.diagnostics + "\"";
        });
        return sample;
    }

private:
    tracing::Trace& trace() {
        static tracing::Trace nullTrace;
        return trace_ != nullptr ? *trace_ : nullTrace;
    }

    ADLXHelper helper_;
    IADLXGPUPtr gpu_;
    IADLXPerformanceMonitoringServicesPtr performanceMonitoring_;
    IADLXGPUMetricsSupportPtr metricsSupport_;
    tracing::Trace* trace_ = nullptr;
    std::string gpuName_;
    std::string diagnostics_ = "ADLX provider not initialized.";
    std::optional<double> totalVramGb_;
    bool temperatureSupported_ = false;
    bool clockSupported_ = false;
    bool fanSupported_ = false;
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(tracing::Trace* trace) {
    return std::make_unique<AmdAdlxGpuTelemetryProvider>(trace);
}
