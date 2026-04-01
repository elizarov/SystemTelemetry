#include <windows.h>

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <filesystem>

#include "vendor/adlx/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "vendor/adlx/SDK/Include/IPerformanceMonitoring.h"
#include "gpu_vendor.h"

namespace {

using namespace adlx;

std::filesystem::path TracePath() {
    wchar_t modulePath[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
    if (length == 0 || length >= ARRAYSIZE(modulePath)) {
        return std::filesystem::current_path() / L"telemetry_dump_trace.txt";
    }
    return std::filesystem::path(modulePath).parent_path() / L"telemetry_dump_trace.txt";
}

void AppendTrace(const std::wstring& text) {
    const std::wstring line = text + L"\r\n";
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return;
    }

    std::string bytes(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, line.c_str(), static_cast<int>(line.size()), bytes.data(), required, nullptr, nullptr);

    HANDLE file = CreateFileW(
        TracePath().c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart == 0) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        DWORD written = 0;
        WriteFile(file, bom, sizeof(bom), &written, nullptr);
    }

    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
}

std::wstring WideFromAnsi(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return L"";
    }
    const int required = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (required <= 1) {
        return L"";
    }
    std::wstring result(static_cast<size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), required);
    return result;
}

std::wstring FormatResult(const wchar_t* label, ADLX_RESULT result) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%ls=%d", label, static_cast<int>(result));
    return buffer;
}

class AmdAdlxGpuTelemetryProvider final : public GpuVendorTelemetryProvider {
public:
    ~AmdAdlxGpuTelemetryProvider() override {
        metricsSupport_ = nullptr;
        performanceMonitoring_ = nullptr;
        gpu_ = nullptr;
        helper_.Terminate();
    }

    bool Initialize() override {
        AppendTrace(L"amd_adlx:initialize_begin");
        ADLX_RESULT result = helper_.Initialize();
        AppendTrace(L"amd_adlx:helper_initialize");
        if (ADLX_FAILED(result)) {
            AppendTrace(L"amd_adlx:helper_initialize_incompatible_begin");
            result = helper_.InitializeWithIncompatibleDriver();
            AppendTrace(L"amd_adlx:helper_initialize_incompatible_done");
        }
        if (ADLX_FAILED(result) || helper_.GetSystemServices() == nullptr) {
            diagnostics_ = L"ADLX initialization failed: " + FormatResult(L"init", result);
            AppendTrace(L"amd_adlx:initialize_failed");
            return false;
        }

        AppendTrace(L"amd_adlx:get_performance_monitoring_begin");
        result = helper_.GetSystemServices()->GetPerformanceMonitoringServices(&performanceMonitoring_);
        AppendTrace(L"amd_adlx:get_performance_monitoring_done");
        if (ADLX_FAILED(result) || !performanceMonitoring_) {
            diagnostics_ = L"Failed to get ADLX performance monitoring services: " + FormatResult(L"perf", result);
            return false;
        }

        IADLXGPUListPtr gpus;
        AppendTrace(L"amd_adlx:get_gpus_begin");
        result = helper_.GetSystemServices()->GetGPUs(&gpus);
        AppendTrace(L"amd_adlx:get_gpus_done");
        if (ADLX_FAILED(result) || !gpus || gpus->Empty()) {
            diagnostics_ = L"Failed to get AMD GPU list: " + FormatResult(L"gpus", result);
            return false;
        }

        AppendTrace(L"amd_adlx:get_first_gpu_begin");
        result = gpus->At(gpus->Begin(), &gpu_);
        AppendTrace(L"amd_adlx:get_first_gpu_done");
        if (ADLX_FAILED(result) || !gpu_) {
            diagnostics_ = L"Failed to open first AMD GPU: " + FormatResult(L"gpu", result);
            return false;
        }

        const char* name = nullptr;
        if (ADLX_SUCCEEDED(gpu_->Name(&name))) {
            gpuName_ = WideFromAnsi(name);
        }
        if (gpuName_.empty()) {
            gpuName_ = L"AMD GPU";
        }

        AppendTrace(L"amd_adlx:get_supported_metrics_begin");
        result = performanceMonitoring_->GetSupportedGPUMetrics(gpu_, &metricsSupport_);
        AppendTrace(L"amd_adlx:get_supported_metrics_done");
        if (ADLX_FAILED(result) || !metricsSupport_) {
            diagnostics_ = L"Failed to query supported AMD GPU metrics: " + FormatResult(L"support", result);
            return false;
        }

        adlx_bool tempSupported = false;
        adlx_bool clockSupported = false;
        adlx_bool fanSupported = false;
        const ADLX_RESULT tempResult = metricsSupport_->IsSupportedGPUTemperature(&tempSupported);
        const ADLX_RESULT clockResult = metricsSupport_->IsSupportedGPUClockSpeed(&clockSupported);
        const ADLX_RESULT fanResult = metricsSupport_->IsSupportedGPUFanSpeed(&fanSupported);

        std::wstringstream diag;
        diag << L"ADLX GPU=" << gpuName_
             << L" temp_supported=" << (tempSupported ? L"yes" : L"no") << L"(" << static_cast<int>(tempResult) << L")"
             << L" clock_supported=" << (clockSupported ? L"yes" : L"no") << L"(" << static_cast<int>(clockResult) << L")"
             << L" fan_supported=" << (fanSupported ? L"yes" : L"no") << L"(" << static_cast<int>(fanResult) << L")";
        diagnostics_ = diag.str();
        initialized_ = true;
        AppendTrace(L"amd_adlx:initialize_done");
        return true;
    }

    GpuVendorTelemetrySample Sample() override {
        AppendTrace(L"amd_adlx:sample_begin");
        GpuVendorTelemetrySample sample;
        sample.providerName = L"AMD ADLX";
        sample.name = gpuName_;
        sample.diagnostics = diagnostics_;

        if (!initialized_ || !performanceMonitoring_ || !gpu_ || !metricsSupport_) {
            sample.available = false;
            return sample;
        }

        IADLXGPUMetricsPtr metrics;
        AppendTrace(L"amd_adlx:get_current_metrics_begin");
        const ADLX_RESULT metricsResult = performanceMonitoring_->GetCurrentGPUMetrics(gpu_, &metrics);
        AppendTrace(L"amd_adlx:get_current_metrics_done");
        if (ADLX_FAILED(metricsResult) || !metrics) {
            sample.diagnostics = diagnostics_ + L" " + FormatResult(L"current_metrics", metricsResult);
            sample.available = false;
            return sample;
        }

        std::wstringstream status;
        status << diagnostics_ << L" sample:";
        bool hasAnyMetric = false;

        adlx_bool supported = false;
        ADLX_RESULT result = metricsSupport_->IsSupportedGPUTemperature(&supported);
        status << L" temp_support=" << static_cast<int>(result);
        if (ADLX_SUCCEEDED(result) && supported) {
            adlx_double temperature = 0.0;
            AppendTrace(L"amd_adlx:get_temperature_begin");
            result = metrics->GPUTemperature(&temperature);
            AppendTrace(L"amd_adlx:get_temperature_done");
            status << L" temp=" << static_cast<int>(result);
            if (ADLX_SUCCEEDED(result)) {
                sample.temperatureC = temperature;
                hasAnyMetric = true;
            }
        }

        supported = false;
        result = metricsSupport_->IsSupportedGPUClockSpeed(&supported);
        status << L" clock_support=" << static_cast<int>(result);
        if (ADLX_SUCCEEDED(result) && supported) {
            adlx_int clockMhz = 0;
            AppendTrace(L"amd_adlx:get_clock_begin");
            result = metrics->GPUClockSpeed(&clockMhz);
            AppendTrace(L"amd_adlx:get_clock_done");
            status << L" clock=" << static_cast<int>(result);
            if (ADLX_SUCCEEDED(result)) {
                sample.coreClockMhz = static_cast<double>(clockMhz);
                hasAnyMetric = true;
            }
        }

        supported = false;
        result = metricsSupport_->IsSupportedGPUFanSpeed(&supported);
        status << L" fan_support=" << static_cast<int>(result);
        if (ADLX_SUCCEEDED(result) && supported) {
            adlx_int fanRpm = 0;
            AppendTrace(L"amd_adlx:get_fan_begin");
            result = metrics->GPUFanSpeed(&fanRpm);
            AppendTrace(L"amd_adlx:get_fan_done");
            status << L" fan=" << static_cast<int>(result);
            if (ADLX_SUCCEEDED(result)) {
                sample.fanRpm = static_cast<double>(fanRpm);
                hasAnyMetric = true;
            }
        }

        sample.diagnostics = status.str();
        sample.available = hasAnyMetric;
        AppendTrace(L"amd_adlx:sample_done");
        return sample;
    }

private:
    ADLXHelper helper_;
    IADLXGPUPtr gpu_;
    IADLXPerformanceMonitoringServicesPtr performanceMonitoring_;
    IADLXGPUMetricsSupportPtr metricsSupport_;
    std::wstring gpuName_;
    std::wstring diagnostics_ = L"ADLX provider not initialized.";
    bool initialized_ = false;
};

}  // namespace

std::unique_ptr<GpuVendorTelemetryProvider> CreateAmdGpuTelemetryProvider() {
    return std::make_unique<AmdAdlxGpuTelemetryProvider>();
}
