#include "telemetry/impl/collector_gpu.h"

#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/metric_board_binding.h"
#include "telemetry/gpu/gpu_vendor_selection.h"
#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"

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
        state.trace_.WriteFmt(TracePrefix::Telemetry, "pdh_array_prepare status=%ld", static_cast<long>(status));
        return totals;
    }

    state.gpu_.counterArrayBuffer.resize(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(state.gpu_.counterArrayBuffer.data());
    status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            "pdh_array_fetch status=%ld count=%lu",
            static_cast<long>(status),
            static_cast<unsigned long>(itemCount));
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

    state.trace_.WriteFmt(TracePrefix::Telemetry,
        "pdh_array_done status=%ld count=%lu total=value=%.2f total3d=value=%.2f",
        static_cast<long>(status),
        static_cast<unsigned long>(itemCount),
        totals.total,
        totals.total3d);
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

std::optional<double> FindBoardFanRpm(const SystemSnapshot& snapshot, const std::string& logicalName) {
    for (const auto& fan : snapshot.boardFans) {
        if (fan.name == logicalName) {
            return FiniteOptional(fan.metric.value);
        }
    }
    return std::nullopt;
}

void RecordActiveMetricBoardBinding(
    RealTelemetryCollectorState& state, std::string_view metricId, const BoardMetricBindingTarget& target) {
    state.activeMetricBoardBindings_.push_back(MetricBoardBindingUse{std::string(metricId), target});
}

void ApplyBoardGpuFanFallback(RealTelemetryCollectorState& state) {
    if (state.snapshot_.gpu.fan.value.has_value()) {
        return;
    }
    const auto target = ResolveMetricBoardBindingTarget(kGpuFanMetricId);
    if (!target.has_value() || target->kind != BoardMetricBindingKind::Fan) {
        return;
    }
    if (auto fanRpm = FindBoardFanRpm(state.snapshot_, target->logicalName); fanRpm.has_value()) {
        state.snapshot_.gpu.fan.value = *fanRpm;
        state.snapshot_.gpu.fan.unit = ScalarMetricUnit::Rpm;
        RecordActiveMetricBoardBinding(state, kGpuFanMetricId, *target);
    }
}

std::optional<double> FindBoardTemperatureC(const SystemSnapshot& snapshot, const std::string& logicalName) {
    for (const auto& temperature : snapshot.boardTemperatures) {
        if (temperature.name == logicalName) {
            return FiniteOptional(temperature.metric.value);
        }
    }
    return std::nullopt;
}

bool IsSelectedIntelGpu(const RealTelemetryCollectorState& state) {
    return state.gpu_.selectedAdapter.has_value() && SelectGpuVendor(*state.gpu_.selectedAdapter) == GpuVendor::Intel;
}

void ApplyIntelCpuTemperatureFallback(RealTelemetryCollectorState& state) {
    if (state.snapshot_.gpu.temperature.value.has_value() || !IsSelectedIntelGpu(state)) {
        return;
    }
    const auto target = ResolveMetricBoardBindingTarget(kGpuTemperatureMetricId);
    if (!target.has_value() || target->kind != BoardMetricBindingKind::Temperature) {
        return;
    }
    if (auto cpuTemperatureC = FindBoardTemperatureC(state.snapshot_, target->logicalName);
        cpuTemperatureC.has_value()) {
        state.snapshot_.gpu.temperature.value = *cpuTemperatureC;
        state.snapshot_.gpu.temperature.unit = ScalarMetricUnit::Celsius;
        RecordActiveMetricBoardBinding(state, kGpuTemperatureMetricId, *target);
        state.trace_.WriteFmt(
            TracePrefix::Telemetry, "gpu_temperature_cpu_fallback temperature_c=value=%.1f", *cpuTemperatureC);
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

    state.trace_.WriteFmt(TracePrefix::Telemetry,
        "gpu_adapter_selected index=%u vendor_id=0x%04X dedicated_bytes=%llu dedicated_gb=%.2f name=\"%s\"",
        adapter.adapterIndex,
        adapter.vendorId,
        static_cast<unsigned long long>(adapter.dedicatedVideoMemoryBytes),
        state.snapshot_.gpu.vram.totalGb,
        adapter.adapterName.c_str());
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
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            "gpu_provider_initialize_done provider=%s available=%s diagnostics=\"%s\"",
            state.gpu_.providerName.c_str(),
            Trace::BoolText(state.gpu_.providerAvailable),
            state.gpu_.providerDiagnostics.c_str());
    } else {
        const GpuVendorTelemetrySample sample = state.gpu_.provider->Sample();
        state.gpu_.providerName = sample.providerName.empty() ? "GPU vendor" : sample.providerName;
        state.gpu_.providerDiagnostics =
            sample.diagnostics.empty() ? "Provider initialization failed." : sample.diagnostics;
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            "gpu_provider_initialize_failed provider=%s diagnostics=\"%s\"",
            state.gpu_.providerName.c_str(),
            state.gpu_.providerDiagnostics.c_str());
    }
}

}  // namespace

void InitializeGpuCollector(RealTelemetryCollectorState& state) {
    ResetGpuProviderState(state);
    ResolveGpuSelection(state);
    InitializeGpuVendorProvider(state);

    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.query);
    state.trace_.WriteFmt(TracePrefix::Telemetry, "pdh_open gpu_query status=%ld", static_cast<long>(queryStatus));
    const PDH_STATUS loadStatus =
        AddCounterCompat(state.gpu_.query, "\\GPU Engine(*)\\Utilization Percentage", &state.gpu_.loadCounter);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        "pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" status=%ld",
        static_cast<long>(loadStatus));
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
    state.trace_.WriteFmt(TracePrefix::Telemetry, "pdh_collect gpu_query status=%ld", static_cast<long>(collectStatus));

    const PDH_STATUS memoryQueryStatus = PdhOpenQueryW(nullptr, 0, &state.gpu_.memoryQuery);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry, "pdh_open gpu_memory_query status=%ld", static_cast<long>(memoryQueryStatus));
    const PDH_STATUS memoryCounterStatus = AddCounterCompat(
        state.gpu_.memoryQuery, "\\GPU Adapter Memory(*)\\Dedicated Usage", &state.gpu_.dedicatedCounter);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        "pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" status=%ld",
        static_cast<long>(memoryCounterStatus));
    const PDH_STATUS memoryCollectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry, "pdh_collect gpu_memory_query status=%ld", static_cast<long>(memoryCollectStatus));
}

void ReconfigureGpuCollector(RealTelemetryCollectorState& state) {
    state.trace_.WriteFmt(TracePrefix::Telemetry, "gpu_provider_shutdown provider=%s", state.gpu_.providerName.c_str());
    state.gpu_.provider.reset();
    ResetGpuProviderState(state);
    ResolveGpuSelection(state);
    InitializeGpuVendorProvider(state);
}

void UpdateGpuMetrics(RealTelemetryCollectorState& state) {
    state.activeMetricBoardBindings_.clear();
    bool hasVendorLoad = false;
    bool hasVendorVram = false;

    if (state.gpu_.provider != nullptr) {
        const GpuVendorTelemetrySample sample = state.gpu_.provider->Sample();
        hasVendorLoad = sample.loadPercent.has_value();
        hasVendorVram = sample.usedVramGb.has_value();
        ApplyGpuVendorSample(state, sample);
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            "gpu_vendor_sample provider=%s available=%s diagnostics=\"%s\"",
            state.gpu_.providerName.c_str(),
            Trace::BoolText(state.gpu_.providerAvailable),
            state.gpu_.providerDiagnostics.c_str());
    }
    ApplyBoardGpuFanFallback(state);
    ApplyIntelCpuTemperatureFallback(state);

    if (!hasVendorLoad && state.gpu_.query != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
        state.trace_.WriteFmt(TracePrefix::Telemetry, "gpu_collect status=%ld", static_cast<long>(collectStatus));
        const CounterArrayTotals loadTotals = ReadCounterArrayTotals(state, state.gpu_.loadCounter);
        const double load3d = loadTotals.total3d;
        const double loadAll = loadTotals.total;
        state.snapshot_.gpu.loadPercent = ClampFinite(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            "gpu_load load3d=value=%.2f loadAll=value=%.2f selected=value=%.2f",
            load3d,
            loadAll,
            state.snapshot_.gpu.loadPercent);
    }
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::GpuLoad, state.snapshot_.gpu.loadPercent);

    if (!hasVendorVram && state.gpu_.memoryQuery != nullptr) {
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
        state.trace_.WriteFmt(
            TracePrefix::Telemetry, "gpu_memory_collect status=%ld", static_cast<long>(collectStatus));
        const double bytes = SumCounterArray(state, state.gpu_.dedicatedCounter);
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(bytes / (1024.0 * 1024.0 * 1024.0));
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            "gpu_memory bytes=value=%.0f used_gb=value=%.2f",
            bytes,
            state.snapshot_.gpu.vram.usedGb);
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
