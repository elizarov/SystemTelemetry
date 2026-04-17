#include "telemetry/collector_gpu.h"

#include <dxgi.h>

#include <vector>

#include "numeric_safety.h"
#include "telemetry/collector_state.h"
#include "telemetry/collector_support.h"
#include "utf8.h"

namespace {

void Trace(const RealTelemetryCollectorState& state, const char* text) {
    state.trace_.Write(text);
}

void Trace(const RealTelemetryCollectorState& state, const std::string& text) {
    state.trace_.Write(text);
}

double SumCounterArray(RealTelemetryCollectorState& state, PDH_HCOUNTER counter, bool require3d) {
    if (counter == nullptr) {
        return 0.0;
    }
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        Trace(state,
            "telemetry:pdh_array_prepare " + tracing::Trace::FormatPdhStatus("status", status) +
                " require3d=" + tracing::Trace::BoolText(require3d));
        return 0.0;
    }

    std::vector<BYTE> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        Trace(state,
            "telemetry:pdh_array_fetch " + tracing::Trace::FormatPdhStatus("status", status) +
                " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d));
        return 0.0;
    }

    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i) {
        const std::wstring instance = items[i].szName != nullptr ? items[i].szName : L"";
        if (require3d && instance.find(L"engtype_3D") == std::wstring::npos) {
            continue;
        }
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !IsFiniteDouble(items[i].FmtValue.doubleValue)) {
            continue;
        }
        total += items[i].FmtValue.doubleValue;
    }

    Trace(state,
        "telemetry:pdh_array_done " + tracing::Trace::FormatPdhStatus("status", status) +
            " count=" + std::to_string(itemCount) + " require3d=" + tracing::Trace::BoolText(require3d) + " " +
            tracing::Trace::FormatValueDouble("total", total, 2));
    return FiniteNonNegativeOr(total);
}

void ApplyGpuVendorSample(RealTelemetryCollectorState& state, const GpuVendorTelemetrySample& sample) {
    state.gpu_.providerName = sample.providerName.empty() ? "None" : sample.providerName;
    state.gpu_.providerDiagnostics = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    state.gpu_.providerAvailable = sample.available;

    if (sample.name.has_value() && !sample.name->empty()) {
        state.snapshot_.gpu.name = *sample.name;
    }
    state.snapshot_.gpu.temperature.value = FiniteOptional(sample.temperatureC);
    state.snapshot_.gpu.temperature.unit = ScalarMetricUnit::Celsius;
    state.snapshot_.gpu.clock.value = FiniteOptional(sample.coreClockMhz);
    state.snapshot_.gpu.clock.unit = ScalarMetricUnit::Megahertz;
    state.snapshot_.gpu.fan.value = FiniteOptional(sample.fanRpm);
    state.snapshot_.gpu.fan.unit = ScalarMetricUnit::Rpm;
    if (sample.totalVramGb.has_value()) {
        const double totalVramGb = FiniteNonNegativeOr(*sample.totalVramGb);
        if (totalVramGb > 0.0) {
            state.snapshot_.gpu.vram.totalGb = totalVramGb;
        }
    }
}

void InitializeGpuAdapterInfo(RealTelemetryCollectorState& state) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT factoryHr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(factoryHr) || factory == nullptr) {
        char buffer[128];
        sprintf_s(buffer, "telemetry:gpu_adapter_factory hr=0x%08X", static_cast<unsigned int>(factoryHr));
        Trace(state, buffer);
        return;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            Trace(state, "telemetry:gpu_adapter_enum done");
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            char buffer[128];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_enum index=%u hr=0x%08X",
                adapterIndex,
                static_cast<unsigned int>(enumHr));
            Trace(state, buffer);
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        const HRESULT descHr = adapter->GetDesc1(&desc);
        if (SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            const std::string adapterName = Utf8FromWide(desc.Description);
            state.snapshot_.gpu.vram.totalGb =
                static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
            if (state.snapshot_.gpu.name == "GPU" && !adapterName.empty()) {
                state.snapshot_.gpu.name = adapterName;
            }

            char buffer[256];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_selected index=%u hr=0x%08X dedicated_bytes=%llu dedicated_gb=%.2f name=\"%s\"",
                adapterIndex,
                static_cast<unsigned int>(descHr),
                static_cast<unsigned long long>(desc.DedicatedVideoMemory),
                state.snapshot_.gpu.vram.totalGb,
                adapterName.c_str());
            Trace(state, buffer);
            adapter->Release();
            break;
        }

        const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
        char buffer[256];
        sprintf_s(buffer,
            "telemetry:gpu_adapter_skip index=%u hr=0x%08X software=%s name=\"%s\"",
            adapterIndex,
            static_cast<unsigned int>(descHr),
            tracing::Trace::BoolText(SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0).c_str(),
            adapterName.c_str());
        Trace(state, buffer);
        adapter->Release();
    }

    factory->Release();
}

}  // namespace

void InitializeGpuCollector(RealTelemetryCollectorState& state) {
    state.gpu_.provider = CreateGpuVendorTelemetryProvider(&state.trace_);
    if (state.gpu_.provider != nullptr) {
        state.trace_.Write("telemetry:gpu_provider_initialize_begin");
        if (state.gpu_.provider->Initialize()) {
            ApplyGpuVendorSample(state, state.gpu_.provider->Sample());
            state.trace_.Write("telemetry:gpu_provider_initialize_done provider=" + state.gpu_.providerName +
                               " available=" + tracing::Trace::BoolText(state.gpu_.providerAvailable) +
                               " diagnostics=\"" + state.gpu_.providerDiagnostics + "\"");
        } else {
            state.gpu_.providerName = "AMD ADLX";
            state.gpu_.providerDiagnostics = "Provider initialization failed.";
            state.trace_.Write("telemetry:gpu_provider_initialize_failed provider=" + state.gpu_.providerName +
                               " diagnostics=\"" + state.gpu_.providerDiagnostics + "\"");
        }
    } else {
        state.trace_.Write("telemetry:gpu_provider_create result=null");
    }

    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.query);
    state.trace_.Write(
        ("telemetry:pdh_open gpu_query " + tracing::Trace::FormatPdhStatus("status", queryStatus)).c_str());
    const PDH_STATUS loadStatus =
        AddCounterCompat(state.gpu_.query, L"\\GPU Engine(*)\\Utilization Percentage", &state.gpu_.loadCounter);
    state.trace_.Write(("telemetry:pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" " +
                        tracing::Trace::FormatPdhStatus("status", loadStatus))
            .c_str());
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
    state.trace_.Write(
        ("telemetry:pdh_collect gpu_query " + tracing::Trace::FormatPdhStatus("status", collectStatus)).c_str());

    const PDH_STATUS memoryQueryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.memoryQuery);
    state.trace_.Write(
        ("telemetry:pdh_open gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", memoryQueryStatus))
            .c_str());
    const PDH_STATUS memoryCounterStatus = AddCounterCompat(
        state.gpu_.memoryQuery, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &state.gpu_.dedicatedCounter);
    state.trace_.Write(("telemetry:pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" " +
                        tracing::Trace::FormatPdhStatus("status", memoryCounterStatus))
            .c_str());
    const PDH_STATUS memoryCollectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
    state.trace_.Write(
        ("telemetry:pdh_collect gpu_memory_query " + tracing::Trace::FormatPdhStatus("status", memoryCollectStatus))
            .c_str());

    InitializeGpuAdapterInfo(state);
}

void UpdateGpuMetrics(RealTelemetryCollectorState& state) {
    if (state.gpu_.query != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
        Trace(state, "telemetry:gpu_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus));
        const double load3d = SumCounterArray(state, state.gpu_.loadCounter, true);
        const double loadAll = SumCounterArray(state, state.gpu_.loadCounter, false);
        state.snapshot_.gpu.loadPercent = ClampFinite(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        Trace(state,
            "telemetry:gpu_load load3d=" + tracing::Trace::FormatValueDouble("value", load3d, 2) +
                " loadAll=" + tracing::Trace::FormatValueDouble("value", loadAll, 2) +
                " selected=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.gpu.loadPercent, 2));
    }
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.load", state.snapshot_.gpu.loadPercent);

    if (state.gpu_.memoryQuery != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
        Trace(state, "telemetry:gpu_memory_collect " + tracing::Trace::FormatPdhStatus("status", collectStatus));
        const double bytes = SumCounterArray(state, state.gpu_.dedicatedCounter, false);
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(bytes / (1024.0 * 1024.0 * 1024.0));
        Trace(state,
            "telemetry:gpu_memory bytes=" + tracing::Trace::FormatValueDouble("value", bytes, 0) +
                " used_gb=" + tracing::Trace::FormatValueDouble("value", state.snapshot_.gpu.vram.usedGb, 2));
    }

    if (state.gpu_.provider != nullptr) {
        ApplyGpuVendorSample(state, state.gpu_.provider->Sample());
        Trace(state,
            "telemetry:gpu_vendor_sample provider=" + state.gpu_.providerName +
                " available=" + tracing::Trace::BoolText(state.gpu_.providerAvailable) + " diagnostics=\"" +
                state.gpu_.providerDiagnostics + "\"");
    }

    state.retainedHistoryStore_.PushSample(
        state.snapshot_, "gpu.temp", state.snapshot_.gpu.temperature.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.clock", state.snapshot_.gpu.clock.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.fan", state.snapshot_.gpu.fan.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.vram", state.snapshot_.gpu.vram.usedGb);
}
