#include "telemetry/impl/collector_gpu.h"

#include <dxgi.h>
#include <utility>
#include <vector>

#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"
#include "util/utf8.h"

namespace {

constexpr wchar_t kGpuEngine3dMarker[] = L"engtype_3D";  // PDH GPU engine instance names are UTF-16.

void WriteTelemetryTrace(const RealTelemetryCollectorState& state, const char* text) {
    state.trace_.Write(TracePrefix::Telemetry, text);
}

void WriteTelemetryTrace(const RealTelemetryCollectorState& state, const std::string& text) {
    state.trace_.Write(TracePrefix::Telemetry, text);
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
        WriteTelemetryTrace(state, "pdh_array_prepare status=" + PdhStatusCodeString(status));
        return totals;
    }

    state.gpu_.counterArrayBuffer.resize(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(state.gpu_.counterArrayBuffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        state.trace_.WriteLazy(TracePrefix::Telemetry, [&] {
            return "pdh_array_fetch status=" + PdhStatusCodeString(status) + " count=" + std::to_string(itemCount);
        });
        return totals;
    }

    for (DWORD i = 0; i < itemCount; ++i) {
        const wchar_t* instance = items[i].szName;
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !IsFiniteDouble(items[i].FmtValue.doubleValue)) {
            continue;
        }
        totals.total += items[i].FmtValue.doubleValue;
        if (instance != nullptr && wcsstr(instance, kGpuEngine3dMarker) != nullptr) {
            totals.total3d += items[i].FmtValue.doubleValue;
        }
    }

    state.trace_.WriteLazy(TracePrefix::Telemetry, [&] {
        return "pdh_array_done status=" + PdhStatusCodeString(status) + " count=" + std::to_string(itemCount) +
               " total=" + Trace::FormatValueDouble("value", totals.total, 2) +
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
    state.snapshot_.gpu.fps.value = FiniteOptional(sample.fps);
    state.snapshot_.gpu.fps.unit = ScalarMetricUnit::Fps;
    state.snapshot_.gpu.fps.issue =
        sample.fpsPermissionRequired ? ScalarMetricIssue::PermissionRequired : ScalarMetricIssue::None;
    state.snapshot_.gpu.fpsAppName = sample.fpsAppName;
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

void ResetGpuProviderState(RealTelemetryCollectorState& state) {
    state.gpu_.providerName = "None";
    state.gpu_.providerDiagnostics = "Provider not initialized.";
    state.gpu_.providerAvailable = false;
}

void ApplySelectedGpuAdapterInfo(RealTelemetryCollectorState& state) {
    if (!state.gpu_.selectedAdapter.has_value()) {
        state.snapshot_.gpu.name = state.settings_.selection.preferredGpuAdapterName.empty()
                                       ? "GPU"
                                       : state.settings_.selection.preferredGpuAdapterName;
        state.snapshot_.gpu.vram.totalGb = 0.0;
        WriteTelemetryTrace(state, "gpu_adapter_selected none");
        return;
    }

    const GpuVendorInfo& adapter = *state.gpu_.selectedAdapter;
    state.snapshot_.gpu.name = adapter.adapterName.empty() ? "GPU" : adapter.adapterName;
    state.snapshot_.gpu.vram.totalGb =
        static_cast<double>(adapter.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

    char buffer[320];
    sprintf_s(buffer,
        "gpu_adapter_selected index=%u vendor_id=0x%04X dedicated_bytes=%llu dedicated_gb=%.2f name=\"%s\"",
        adapter.adapterIndex,
        adapter.vendorId,
        static_cast<unsigned long long>(adapter.dedicatedVideoMemoryBytes),
        state.snapshot_.gpu.vram.totalGb,
        adapter.adapterName.c_str());
    WriteTelemetryTrace(state, buffer);
}

void ResolveGpuSelection(RealTelemetryCollectorState& state) {
    GpuAdapterSelection selection =
        ResolveGpuAdapterSelection(state.trace_, state.settings_.selection.preferredGpuAdapterName);
    state.gpu_.selectedAdapter = selection.selectedAdapter;
    state.gpu_.adapterCandidates = std::move(selection.candidates);
    state.resolvedSelections_.gpuAdapterName =
        state.gpu_.selectedAdapter.has_value() ? state.gpu_.selectedAdapter->adapterName : std::string();
    state.snapshot_.gpu = GpuTelemetry{};
    ApplySelectedGpuAdapterInfo(state);
}

void InitializeGpuVendorProvider(RealTelemetryCollectorState& state) {
    state.gpu_.provider = CreateGpuVendorTelemetryProvider(state.trace_, state.gpu_.selectedAdapter);
    if (state.gpu_.provider == nullptr) {
        state.trace_.Write(TracePrefix::Telemetry, "gpu_provider_create result=null");
        return;
    }

    state.trace_.Write(TracePrefix::Telemetry, "gpu_provider_initialize_begin");
    if (state.gpu_.provider->Initialize()) {
        ApplyGpuVendorSample(state, state.gpu_.provider->Sample());
        state.trace_.Write(TracePrefix::Telemetry,
            "gpu_provider_initialize_done provider=" + state.gpu_.providerName +
                " available=" + Trace::BoolText(state.gpu_.providerAvailable) + " diagnostics=\"" +
                state.gpu_.providerDiagnostics + "\"");
    } else {
        const GpuVendorTelemetrySample sample = state.gpu_.provider->Sample();
        state.gpu_.providerName = sample.providerName.empty() ? "GPU vendor" : sample.providerName;
        state.gpu_.providerDiagnostics =
            sample.diagnostics.empty() ? "Provider initialization failed." : sample.diagnostics;
        state.trace_.Write(TracePrefix::Telemetry,
            "gpu_provider_initialize_failed provider=" + state.gpu_.providerName + " diagnostics=\"" +
                state.gpu_.providerDiagnostics + "\"");
    }
}

}  // namespace

void InitializeGpuCollector(RealTelemetryCollectorState& state) {
    ResetGpuProviderState(state);
    ResolveGpuSelection(state);
    InitializeGpuVendorProvider(state);

    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.query);
    state.trace_.Write(
        TracePrefix::Telemetry, ("pdh_open gpu_query status=" + PdhStatusCodeString(queryStatus)).c_str());
    const PDH_STATUS loadStatus =
        AddCounterCompat(state.gpu_.query, "\\GPU Engine(*)\\Utilization Percentage", &state.gpu_.loadCounter);
    state.trace_.Write(TracePrefix::Telemetry,
        ("pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" status=" +
            PdhStatusCodeString(loadStatus))
            .c_str());
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
    state.trace_.Write(
        TracePrefix::Telemetry, ("pdh_collect gpu_query status=" + PdhStatusCodeString(collectStatus)).c_str());

    const PDH_STATUS memoryQueryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.memoryQuery);
    state.trace_.Write(
        TracePrefix::Telemetry, ("pdh_open gpu_memory_query status=" + PdhStatusCodeString(memoryQueryStatus)).c_str());
    const PDH_STATUS memoryCounterStatus = AddCounterCompat(
        state.gpu_.memoryQuery, "\\GPU Adapter Memory(*)\\Dedicated Usage", &state.gpu_.dedicatedCounter);
    state.trace_.Write(TracePrefix::Telemetry,
        ("pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" status=" +
            PdhStatusCodeString(memoryCounterStatus))
            .c_str());
    const PDH_STATUS memoryCollectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
    state.trace_.Write(TracePrefix::Telemetry,
        ("pdh_collect gpu_memory_query status=" + PdhStatusCodeString(memoryCollectStatus)).c_str());
}

void ReconfigureGpuCollector(RealTelemetryCollectorState& state) {
    state.trace_.Write(TracePrefix::Telemetry, "gpu_provider_shutdown provider=" + state.gpu_.providerName);
    state.gpu_.provider.reset();
    ResetGpuProviderState(state);
    ResolveGpuSelection(state);
    InitializeGpuVendorProvider(state);
}

void UpdateGpuMetrics(RealTelemetryCollectorState& state) {
    bool hasVendorLoad = false;
    bool hasVendorVram = false;

    if (state.gpu_.provider != nullptr) {
        const GpuVendorTelemetrySample sample = state.gpu_.provider->Sample();
        hasVendorLoad = sample.loadPercent.has_value();
        hasVendorVram = sample.usedVramGb.has_value();
        ApplyGpuVendorSample(state, sample);
        state.trace_.WriteLazy(TracePrefix::Telemetry, [&] {
            return "gpu_vendor_sample provider=" + state.gpu_.providerName +
                   " available=" + Trace::BoolText(state.gpu_.providerAvailable) + " diagnostics=\"" +
                   state.gpu_.providerDiagnostics + "\"";
        });
    }

    if (!hasVendorLoad && state.gpu_.query != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
        state.trace_.WriteLazy(
            TracePrefix::Telemetry, [&] { return "gpu_collect status=" + PdhStatusCodeString(collectStatus); });
        const CounterArrayTotals loadTotals = ReadCounterArrayTotals(state, state.gpu_.loadCounter);
        const double load3d = loadTotals.total3d;
        const double loadAll = loadTotals.total;
        state.snapshot_.gpu.loadPercent = ClampFinite(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        state.trace_.WriteLazy(TracePrefix::Telemetry, [&] {
            return "gpu_load load3d=" + Trace::FormatValueDouble("value", load3d, 2) +
                   " loadAll=" + Trace::FormatValueDouble("value", loadAll, 2) +
                   " selected=" + Trace::FormatValueDouble("value", state.snapshot_.gpu.loadPercent, 2);
        });
    }
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuLoad, state.snapshot_.gpu.loadPercent);

    if (!hasVendorVram && state.gpu_.memoryQuery != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
        state.trace_.WriteLazy(
            TracePrefix::Telemetry, [&] { return "gpu_memory_collect status=" + PdhStatusCodeString(collectStatus); });
        const double bytes = SumCounterArray(state, state.gpu_.dedicatedCounter);
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(bytes / (1024.0 * 1024.0 * 1024.0));
        state.trace_.WriteLazy(TracePrefix::Telemetry, [&] {
            return "gpu_memory bytes=" + Trace::FormatValueDouble("value", bytes, 0) +
                   " used_gb=" + Trace::FormatValueDouble("value", state.snapshot_.gpu.vram.usedGb, 2);
        });
    }

    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuTemperature, state.snapshot_.gpu.temperature.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuClock, state.snapshot_.gpu.clock.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuFan, state.snapshot_.gpu.fan.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuFps, state.snapshot_.gpu.fps.value.value_or(0.0));
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuVram, state.snapshot_.gpu.vram.usedGb);
}
