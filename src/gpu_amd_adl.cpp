#include <windows.h>

#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>

#include "vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "vendor/adlx/SDK/Include/IPerformanceMonitoring.h"
#include "gpu_vendor.h"
#include "utf8.h"

namespace {

using namespace adlx;

void AppendTrace(std::ostream* traceStream, const char* text) {
    if (traceStream == nullptr) {
        return;
    }
    (*traceStream) << "[trace] " << text << '\n';
    traceStream->flush();
}

void AppendTrace(std::ostream* traceStream, const std::string& text) {
    AppendTrace(traceStream, text.c_str());
}

std::string Utf8FromAnsi(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    return Utf8FromCodePage(text, CP_ACP);
}

std::string FormatResult(const char* label, ADLX_RESULT result) {
    char buffer[64];
    sprintf_s(buffer, "%s=%d", label, static_cast<int>(result));
    return buffer;
}

std::string BoolText(bool value) {
    return value ? "yes" : "no";
}

class AmdAdlxGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    explicit AmdAdlxGpuTelemetryProvider(std::ostream* traceStream) : traceStream_(traceStream) {}

    ~AmdAdlxGpuTelemetryProvider() override {
        metricsSupport_ = nullptr;
        performanceMonitoring_ = nullptr;
        gpu_ = nullptr;
        helper_.Terminate();
    }

    bool Initialize() override {
        AppendTrace(traceStream_, "amd_adlx:initialize_begin");
        ADLX_RESULT result = helper_.Initialize();
        AppendTrace(traceStream_, "amd_adlx:helper_initialize " + FormatResult("result", result));
        if (ADLX_FAILED(result)) {
            AppendTrace(traceStream_, "amd_adlx:helper_initialize_incompatible_begin " + FormatResult("result", result));
            result = helper_.InitializeWithIncompatibleDriver();
            AppendTrace(traceStream_, "amd_adlx:helper_initialize_incompatible_done " + FormatResult("result", result));
        }
        if (ADLX_FAILED(result) || helper_.GetSystemServices() == nullptr) {
            diagnostics_ = "ADLX initialization failed: " + FormatResult("init", result);
            AppendTrace(traceStream_, "amd_adlx:initialize_failed " + diagnostics_);
            return false;
        }

        AppendTrace(traceStream_, "amd_adlx:get_performance_monitoring_begin");
        result = helper_.GetSystemServices()->GetPerformanceMonitoringServices(&performanceMonitoring_);
        AppendTrace(traceStream_, "amd_adlx:get_performance_monitoring_done " + FormatResult("result", result) +
            " available=" + BoolText(performanceMonitoring_ != nullptr));
        if (ADLX_FAILED(result) || !performanceMonitoring_) {
            diagnostics_ = "Failed to get ADLX performance monitoring services: " + FormatResult("perf", result);
            AppendTrace(traceStream_, "amd_adlx:get_performance_monitoring_failed " + diagnostics_);
            return false;
        }

        IADLXGPUListPtr gpus;
        AppendTrace(traceStream_, "amd_adlx:get_gpus_begin");
        result = helper_.GetSystemServices()->GetGPUs(&gpus);
        AppendTrace(traceStream_, "amd_adlx:get_gpus_done " + FormatResult("result", result) +
            " available=" + BoolText(gpus != nullptr));
        if (ADLX_FAILED(result) || !gpus || gpus->Empty()) {
            diagnostics_ = "Failed to get AMD GPU list: " + FormatResult("gpus", result);
            AppendTrace(traceStream_, "amd_adlx:get_gpus_failed " + diagnostics_);
            return false;
        }

        AppendTrace(traceStream_, "amd_adlx:get_first_gpu_begin");
        result = gpus->At(gpus->Begin(), &gpu_);
        AppendTrace(traceStream_, "amd_adlx:get_first_gpu_done " + FormatResult("result", result) +
            " available=" + BoolText(gpu_ != nullptr));
        if (ADLX_FAILED(result) || !gpu_) {
            diagnostics_ = "Failed to open first AMD GPU: " + FormatResult("gpu", result);
            AppendTrace(traceStream_, "amd_adlx:get_first_gpu_failed " + diagnostics_);
            return false;
        }

        const char* name = nullptr;
        const ADLX_RESULT nameResult = gpu_->Name(&name);
        AppendTrace(traceStream_, "amd_adlx:get_gpu_name " + FormatResult("result", nameResult) +
            " has_name=" + BoolText(name != nullptr && name[0] != '\0'));
        if (ADLX_SUCCEEDED(nameResult)) {
            gpuName_ = Utf8FromAnsi(name);
        }
        if (gpuName_.empty()) {
            gpuName_ = "AMD GPU";
        }

        AppendTrace(traceStream_, "amd_adlx:get_supported_metrics_begin");
        result = performanceMonitoring_->GetSupportedGPUMetrics(gpu_, &metricsSupport_);
        AppendTrace(traceStream_, "amd_adlx:get_supported_metrics_done " + FormatResult("result", result) +
            " available=" + BoolText(metricsSupport_ != nullptr));
        if (ADLX_FAILED(result) || !metricsSupport_) {
            diagnostics_ = "Failed to query supported AMD GPU metrics: " + FormatResult("support", result);
            AppendTrace(traceStream_, "amd_adlx:get_supported_metrics_failed " + diagnostics_);
            return false;
        }

        adlx_bool tempSupported = false;
        adlx_bool clockSupported = false;
        adlx_bool fanSupported = false;
        const ADLX_RESULT tempResult = metricsSupport_->IsSupportedGPUTemperature(&tempSupported);
        const ADLX_RESULT clockResult = metricsSupport_->IsSupportedGPUClockSpeed(&clockSupported);
        const ADLX_RESULT fanResult = metricsSupport_->IsSupportedGPUFanSpeed(&fanSupported);

        std::ostringstream diag;
        diag << "ADLX GPU=" << gpuName_
             << " temp_supported=" << (tempSupported ? "yes" : "no") << "(" << static_cast<int>(tempResult) << ")"
             << " clock_supported=" << (clockSupported ? "yes" : "no") << "(" << static_cast<int>(clockResult) << ")"
             << " fan_supported=" << (fanSupported ? "yes" : "no") << "(" << static_cast<int>(fanResult) << ")";
        diagnostics_ = diag.str();
        initialized_ = true;
        AppendTrace(traceStream_, "amd_adlx:initialize_done diagnostics=\"" + diagnostics_ + "\"");
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        AppendTrace(traceStream_, "amd_adlx:sample_begin");
        GpuVendorTelemetrySample sample;
        sample.providerName = "AMD ADLX";
        sample.name = gpuName_;
        sample.diagnostics = diagnostics_;

        if (!initialized_ || !performanceMonitoring_ || !gpu_ || !metricsSupport_) {
            sample.available = false;
            return sample;
        }

        IADLXGPUMetricsPtr metrics;
        AppendTrace(traceStream_, "amd_adlx:get_current_metrics_begin");
        const ADLX_RESULT metricsResult = performanceMonitoring_->GetCurrentGPUMetrics(gpu_, &metrics);
        AppendTrace(traceStream_, "amd_adlx:get_current_metrics_done " + FormatResult("result", metricsResult) +
            " available=" + BoolText(metrics != nullptr));
        if (ADLX_FAILED(metricsResult) || !metrics) {
            sample.diagnostics = diagnostics_ + " " + FormatResult("current_metrics", metricsResult);
            sample.available = false;
            AppendTrace(traceStream_, "amd_adlx:get_current_metrics_failed diagnostics=\"" + sample.diagnostics + "\"");
            return sample;
        }

        std::ostringstream status;
        status << diagnostics_ << " sample:";
        bool hasAnyMetric = false;

        adlx_bool supported = false;
        ADLX_RESULT result = metricsSupport_->IsSupportedGPUTemperature(&supported);
        status << " temp_support=" << static_cast<int>(result);
        if (ADLX_SUCCEEDED(result) && supported) {
            adlx_double temperature = 0.0;
            AppendTrace(traceStream_, "amd_adlx:get_temperature_begin");
            result = metrics->GPUTemperature(&temperature);
            {
                char buffer[128];
                sprintf_s(buffer, "amd_adlx:get_temperature_done result=%d value=%.1f",
                    static_cast<int>(result), temperature);
                AppendTrace(traceStream_, buffer);
            }
            status << " temp=" << static_cast<int>(result);
            if (ADLX_SUCCEEDED(result)) {
                sample.temperatureC = temperature;
                hasAnyMetric = true;
            }
        }

        supported = false;
        result = metricsSupport_->IsSupportedGPUClockSpeed(&supported);
        status << " clock_support=" << static_cast<int>(result);
        if (ADLX_SUCCEEDED(result) && supported) {
            adlx_int clockMhz = 0;
            AppendTrace(traceStream_, "amd_adlx:get_clock_begin");
            result = metrics->GPUClockSpeed(&clockMhz);
            {
                char buffer[128];
                sprintf_s(buffer, "amd_adlx:get_clock_done result=%d value=%d",
                    static_cast<int>(result), static_cast<int>(clockMhz));
                AppendTrace(traceStream_, buffer);
            }
            status << " clock=" << static_cast<int>(result);
            if (ADLX_SUCCEEDED(result)) {
                sample.coreClockMhz = static_cast<double>(clockMhz);
                hasAnyMetric = true;
            }
        }

        supported = false;
        result = metricsSupport_->IsSupportedGPUFanSpeed(&supported);
        status << " fan_support=" << static_cast<int>(result);
        if (ADLX_SUCCEEDED(result) && supported) {
            adlx_int fanRpm = 0;
            AppendTrace(traceStream_, "amd_adlx:get_fan_begin");
            result = metrics->GPUFanSpeed(&fanRpm);
            {
                char buffer[128];
                sprintf_s(buffer, "amd_adlx:get_fan_done result=%d value=%d",
                    static_cast<int>(result), static_cast<int>(fanRpm));
                AppendTrace(traceStream_, buffer);
            }
            status << " fan=" << static_cast<int>(result);
            if (ADLX_SUCCEEDED(result)) {
                sample.fanRpm = static_cast<double>(fanRpm);
                hasAnyMetric = true;
            }
        }

        sample.diagnostics = status.str();
        sample.available = hasAnyMetric;
        AppendTrace(traceStream_, "amd_adlx:sample_done available=" + BoolText(sample.available) +
            " diagnostics=\"" + sample.diagnostics + "\"");
        return sample;
    }

private:
    ADLXHelper helper_;
    IADLXGPUPtr gpu_;
    IADLXPerformanceMonitoringServicesPtr performanceMonitoring_;
    IADLXGPUMetricsSupportPtr metricsSupport_;
    std::ostream* traceStream_ = nullptr;
    std::string gpuName_;
    std::string diagnostics_ = "ADLX provider not initialized.";
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider(std::ostream* traceStream) {
    return std::make_unique<AmdAdlxGpuTelemetryProvider>(traceStream);
}
