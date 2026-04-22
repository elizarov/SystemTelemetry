#include "telemetry/impl/collector_gpu.h"

#include <dxgi.h>
#include <vector>

#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"
#include "util/utf8.h"

namespace {

void WriteTelemetryTrace(const RealTelemetryCollectorState& state, const char* text) {
    state.trace_.Write(text);
}

void WriteTelemetryTrace(const RealTelemetryCollectorState& state, const std::string& text) {
    state.trace_.Write(text);
}

struct CounterArrayTotals {
    double total = 0.0;
    double total3d = 0.0;
};

CounterArrayTotals ReadCounterArrayTotals(RealTelemetryCollectorState& state, PDH_HCOUNTER counter) {
    CounterArrayTotals totals;
    if (counter == nullptr) {
        return totals;
    }
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        WriteTelemetryTrace(state, "telemetry:pdh_array_prepare status=" + PdhStatusCodeString(status));
        return totals;
    }

    state.gpu_.counterArrayBuffer.resize(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(state.gpu_.counterArrayBuffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        state.trace_.WriteLazy([&] {
            return "telemetry:pdh_array_fetch status=" + PdhStatusCodeString(status) +
                   " count=" + std::to_string(itemCount);
        });
        return totals;
    }

    for (DWORD i = 0; i < itemCount; ++i) {
        const wchar_t* instance = items[i].szName;
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !IsFiniteDouble(items[i].FmtValue.doubleValue)) {
            continue;
        }
        totals.total += items[i].FmtValue.doubleValue;
        if (instance != nullptr && wcsstr(instance, L"engtype_3D") != nullptr) {
            totals.total3d += items[i].FmtValue.doubleValue;
        }
    }

    state.trace_.WriteLazy([&] {
        return "telemetry:pdh_array_done status=" + PdhStatusCodeString(status) +
               " count=" + std::to_string(itemCount) + " total=" + Trace::FormatValueDouble("value", totals.total, 2) +
               " total3d=" + Trace::FormatValueDouble("value", totals.total3d, 2);
    });
    totals.total = FiniteNonNegativeOr(totals.total);
    totals.total3d = FiniteNonNegativeOr(totals.total3d);
    return totals;
}

double SumCounterArray(RealTelemetryCollectorState& state, PDH_HCOUNTER counter) {
    return ReadCounterArrayTotals(state, counter).total;
}

void ApplyGpuVendorSample(RealTelemetryCollectorState& state, const GpuVendorTelemetrySample& sample) {
    state.gpu_.providerName = sample.providerName.empty() ? "None" : sample.providerName;
    state.gpu_.providerDiagnostics = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    state.gpu_.providerAvailable = sample.available;

    if (sample.name.has_value() && !sample.name->empty()) {
        state.snapshot_.gpu.name = *sample.name;
    }
    if (sample.loadPercent.has_value()) {
        state.snapshot_.gpu.loadPercent = ClampFinite(*sample.loadPercent, 0.0, 100.0);
    }
    state.snapshot_.gpu.temperature.value = FiniteOptional(sample.temperatureC);
    state.snapshot_.gpu.temperature.unit = ScalarMetricUnit::Celsius;
    state.snapshot_.gpu.clock.value = FiniteOptional(sample.coreClockMhz);
    state.snapshot_.gpu.clock.unit = ScalarMetricUnit::Megahertz;
    state.snapshot_.gpu.fan.value = FiniteOptional(sample.fanRpm);
    state.snapshot_.gpu.fan.unit = ScalarMetricUnit::Rpm;
    if (sample.usedVramGb.has_value()) {
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(*sample.usedVramGb);
    }
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
        WriteTelemetryTrace(state, buffer);
        return;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT enumHr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (enumHr == DXGI_ERROR_NOT_FOUND) {
            WriteTelemetryTrace(state, "telemetry:gpu_adapter_enum done");
            break;
        }
        if (FAILED(enumHr) || adapter == nullptr) {
            char buffer[128];
            sprintf_s(buffer,
                "telemetry:gpu_adapter_enum index=%u hr=0x%08X",
                adapterIndex,
                static_cast<unsigned int>(enumHr));
            WriteTelemetryTrace(state, buffer);
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
            WriteTelemetryTrace(state, buffer);
            adapter->Release();
            break;
        }

        const std::string adapterName = SUCCEEDED(descHr) ? Utf8FromWide(desc.Description) : std::string();
        char buffer[256];
        sprintf_s(buffer,
            "telemetry:gpu_adapter_skip index=%u hr=0x%08X software=%s name=\"%s\"",
            adapterIndex,
            static_cast<unsigned int>(descHr),
            Trace::BoolText(SUCCEEDED(descHr) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0).c_str(),
            adapterName.c_str());
        WriteTelemetryTrace(state, buffer);
        adapter->Release();
    }

    factory->Release();
}

}  // namespace

void InitializeGpuCollector(RealTelemetryCollectorState& state) {
    state.gpu_.provider = CreateGpuVendorTelemetryProvider(state.trace_);
    if (state.gpu_.provider != nullptr) {
        state.trace_.Write("telemetry:gpu_provider_initialize_begin");
        if (state.gpu_.provider->Initialize()) {
            ApplyGpuVendorSample(state, state.gpu_.provider->Sample());
            state.trace_.Write("telemetry:gpu_provider_initialize_done provider=" + state.gpu_.providerName +
                               " available=" + Trace::BoolText(state.gpu_.providerAvailable) + " diagnostics=\"" +
                               state.gpu_.providerDiagnostics + "\"");
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
    state.trace_.Write(("telemetry:pdh_open gpu_query status=" + PdhStatusCodeString(queryStatus)).c_str());
    const PDH_STATUS loadStatus =
        AddCounterCompat(state.gpu_.query, L"\\GPU Engine(*)\\Utilization Percentage", &state.gpu_.loadCounter);
    state.trace_.Write(("telemetry:pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" status=" +
                        PdhStatusCodeString(loadStatus))
            .c_str());
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
    state.trace_.Write(("telemetry:pdh_collect gpu_query status=" + PdhStatusCodeString(collectStatus)).c_str());

    const PDH_STATUS memoryQueryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.memoryQuery);
    state.trace_.Write(
        ("telemetry:pdh_open gpu_memory_query status=" + PdhStatusCodeString(memoryQueryStatus)).c_str());
    const PDH_STATUS memoryCounterStatus = AddCounterCompat(
        state.gpu_.memoryQuery, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &state.gpu_.dedicatedCounter);
    state.trace_.Write(("telemetry:pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" status=" +
                        PdhStatusCodeString(memoryCounterStatus))
            .c_str());
    const PDH_STATUS memoryCollectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
    state.trace_.Write(
        ("telemetry:pdh_collect gpu_memory_query status=" + PdhStatusCodeString(memoryCollectStatus)).c_str());

    InitializeGpuAdapterInfo(state);
}

void UpdateGpuMetrics(RealTelemetryCollectorState& state) {
    bool hasVendorLoad = false;
    bool hasVendorVram = false;

    if (state.gpu_.provider != nullptr) {
        const GpuVendorTelemetrySample sample = state.gpu_.provider->Sample();
        hasVendorLoad = sample.loadPercent.has_value();
        hasVendorVram = sample.usedVramGb.has_value();
        ApplyGpuVendorSample(state, sample);
        state.trace_.WriteLazy([&] {
            return "telemetry:gpu_vendor_sample provider=" + state.gpu_.providerName +
                   " available=" + Trace::BoolText(state.gpu_.providerAvailable) + " diagnostics=\"" +
                   state.gpu_.providerDiagnostics + "\"";
        });
    }

    if (!hasVendorLoad && state.gpu_.query != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
        state.trace_.WriteLazy([&] { return "telemetry:gpu_collect status=" + PdhStatusCodeString(collectStatus); });
        const CounterArrayTotals loadTotals = ReadCounterArrayTotals(state, state.gpu_.loadCounter);
        const double load3d = loadTotals.total3d;
        const double loadAll = loadTotals.total;
        state.snapshot_.gpu.loadPercent = ClampFinite(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        state.trace_.WriteLazy([&] {
            return "telemetry:gpu_load load3d=" + Trace::FormatValueDouble("value", load3d, 2) +
                   " loadAll=" + Trace::FormatValueDouble("value", loadAll, 2) +
                   " selected=" + Trace::FormatValueDouble("value", state.snapshot_.gpu.loadPercent, 2);
        });
    }
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.load", state.snapshot_.gpu.loadPercent);

    if (!hasVendorVram && state.gpu_.memoryQuery != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
        state.trace_.WriteLazy(
            [&] { return "telemetry:gpu_memory_collect status=" + PdhStatusCodeString(collectStatus); });
        const double bytes = SumCounterArray(state, state.gpu_.dedicatedCounter);
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(bytes / (1024.0 * 1024.0 * 1024.0));
        state.trace_.WriteLazy([&] {
            return "telemetry:gpu_memory bytes=" + Trace::FormatValueDouble("value", bytes, 0) +
                   " used_gb=" + Trace::FormatValueDouble("value", state.snapshot_.gpu.vram.usedGb, 2);
        });
    }

    state.retainedHistoryStore_.PushSample(
        state.snapshot_, "gpu.temp", state.snapshot_.gpu.temperature.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.clock", state.snapshot_.gpu.clock.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.fan", state.snapshot_.gpu.fan.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(state.snapshot_, "gpu.vram", state.snapshot_.gpu.vram.usedGb);
}
