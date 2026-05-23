#include "telemetry/impl/collector_gpu.h"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/metric_board_binding.h"
#include "telemetry/fps/fps_service_client_provider.h"
#include "telemetry/gpu/gpu_vendor_selection.h"
#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"
#include "util/resource_strings.h"

namespace {

constexpr char kPdhAllGpuAdaptersFilterLabel[] = "all";
constexpr char kGpuEngine3dMarker[] = "engtype_3D";

struct CounterArrayTotals {
    double total = 0.0;
    double total3d = 0.0;
    DWORD matchedCount = 0;
};

std::optional<std::string> SelectedGpuPdhLuidToken(const RealTelemetryCollectorState& state) {
    return state.gpu_.selectedAdapter.has_value() ? GpuAdapterPdhLuidToken(*state.gpu_.selectedAdapter) : std::nullopt;
}

char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool MatchesPdhInstanceFilter(const char* instance, std::string_view filter) {
    if (filter.empty()) {
        return true;
    }
    if (instance == nullptr) {
        return false;
    }

    for (const char* cursor = instance; *cursor != '\0'; ++cursor) {
        size_t matched = 0;
        while (
            matched < filter.size() &&
            cursor[matched] != '\0' &&
            LowerAscii(cursor[matched]) == LowerAscii(filter[matched])
        ) {
            ++matched;
        }
        if (matched == filter.size()) {
            return true;
        }
    }
    return false;
}

std::string FormatOptionalFps(std::optional<double> value) {
    return value.has_value() ? Trace::FormatValueDouble("fps", *value, 1) : std::string("fps=N/A");
}

CounterArrayTotals ReadCounterArrayTotals(
    RealTelemetryCollectorState& state,
    PDH_HCOUNTER counter,
    std::string_view instanceFilter,
    const char* filter
) {
    CounterArrayTotals totals;
    if (counter == nullptr) {
        return totals;
    }
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayA(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA) {
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("pdh_array_prepare status=%ld"),
            static_cast<long>(status)
        );
        return totals;
    }

    state.gpu_.counterArrayBuffer.resize(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(state.gpu_.counterArrayBuffer.data());
    status = PdhGetFormattedCounterArrayA(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS) {
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("pdh_array_fetch status=%ld count=%lu"),
            static_cast<long>(status),
            static_cast<unsigned long>(itemCount)
        );
        return totals;
    }

    for (DWORD i = 0; i < itemCount; ++i) {
        const char* instance = items[i].szName;
        if (!MatchesPdhInstanceFilter(instance, instanceFilter)) {
            continue;
        }
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS || !IsFiniteDouble(items[i].FmtValue.doubleValue)) {
            continue;
        }
        ++totals.matchedCount;
        totals.total += items[i].FmtValue.doubleValue;
        if (instance != nullptr && std::strstr(instance, kGpuEngine3dMarker) != nullptr) {
            totals.total3d += items[i].FmtValue.doubleValue;
        }
    }

    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_array_done status=%ld count=%lu matched=%lu filter=\"%s\" total=value=%.2f total3d=value=%.2f"),
        static_cast<long>(status),
        static_cast<unsigned long>(itemCount),
        static_cast<unsigned long>(totals.matchedCount),
        filter,
        totals.total,
        totals.total3d
    );
    totals.total = FiniteNonNegativeOr(totals.total);
    totals.total3d = FiniteNonNegativeOr(totals.total3d);
    return totals;
}

double SumCounterArray(
    RealTelemetryCollectorState& state,
    PDH_HCOUNTER counter,
    std::string_view instanceFilter,
    const char* filter
) {
    return ReadCounterArrayTotals(state, counter, instanceFilter, filter).total;
}

void ApplyGpuVendorSample(RealTelemetryCollectorState& state, const GpuVendorTelemetrySample& sample) {
    state.gpu_.providerName = sample.providerName.empty() ? "None" : sample.providerName;
    state.gpu_.providerDiagnostics =
        sample.diagnostics.empty() ? ResourceStringText(RES_STR("(none)")) : sample.diagnostics;
    state.gpu_.providerAvailable = sample.available;

    if (sample.name.has_value() && !sample.name->empty()) {
        state.snapshot_.gpu.name = *sample.name;
    }
    if (sample.loadPercent.has_value()) {
        state.snapshot_.gpu.loadPercent = ClampFinite(*sample.loadPercent, 0.0, 100.0);
    }
    state.snapshot_.gpu.temperature.value = FiniteOptional(sample.temperatureC);
    state.snapshot_.gpu.temperature.unit = ScalarMetricUnit::Celsius;
    state.snapshot_.gpu.temperature.issue = ScalarMetricIssue::None;
    state.snapshot_.gpu.clock.value = FiniteOptional(sample.coreClockMhz);
    state.snapshot_.gpu.clock.unit = ScalarMetricUnit::Megahertz;
    state.snapshot_.gpu.fan.value = FiniteOptional(sample.fanRpm);
    state.snapshot_.gpu.fan.unit = ScalarMetricUnit::Rpm;
    state.snapshot_.gpu.fan.issue = ScalarMetricIssue::None;
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

std::optional<ScalarMetric> FindBoardFanMetric(const SystemSnapshot& snapshot, const std::string& logicalName) {
    for (const auto& fan : snapshot.boardFans) {
        if (fan.name == logicalName) {
            ScalarMetric metric = fan.metric;
            metric.value = FiniteOptional(metric.value);
            metric.unit = ScalarMetricUnit::Rpm;
            if (metric.value.has_value() || metric.issue != ScalarMetricIssue::None) {
                return metric;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void RecordActiveMetricBoardBinding(
    RealTelemetryCollectorState& state,
    std::string_view metricId,
    const BoardMetricBindingTarget& target
) {
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
    if (auto fanMetric = FindBoardFanMetric(state.snapshot_, target->logicalName); fanMetric.has_value()) {
        state.snapshot_.gpu.fan = *fanMetric;
        RecordActiveMetricBoardBinding(state, kGpuFanMetricId, *target);
    }
}

std::optional<ScalarMetric> FindBoardTemperatureMetric(const SystemSnapshot& snapshot, const std::string& logicalName) {
    for (const auto& temperature : snapshot.boardTemperatures) {
        if (temperature.name == logicalName) {
            ScalarMetric metric = temperature.metric;
            metric.value = FiniteOptional(metric.value);
            metric.unit = ScalarMetricUnit::Celsius;
            if (metric.value.has_value() || metric.issue != ScalarMetricIssue::None) {
                return metric;
            }
            return std::nullopt;
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
    if (
        auto cpuTemperature = FindBoardTemperatureMetric(state.snapshot_, target->logicalName);
        cpuTemperature.has_value()
    ) {
        state.snapshot_.gpu.temperature = *cpuTemperature;
        RecordActiveMetricBoardBinding(state, kGpuTemperatureMetricId, *target);
        if (cpuTemperature->value.has_value()) {
            state.trace_.WriteFmt(
                TracePrefix::Telemetry,
                RES_STR("gpu_temperature_cpu_fallback temperature_c=value=%.1f"),
                *cpuTemperature->value
            );
        } else if (cpuTemperature->issue == ScalarMetricIssue::PermissionRequired) {
            state.trace_.Write(
                TracePrefix::Telemetry,
                RES_STR("gpu_temperature_cpu_fallback issue=permission_required")
            );
        }
    }
}

void ResetGpuProviderState(RealTelemetryCollectorState& state) {
    state.gpu_.providerName = "None";
    state.gpu_.providerDiagnostics = ResourceStringText(RES_STR("Provider not initialized."));
    state.gpu_.providerAvailable = false;
}

void ApplySelectedGpuAdapterInfo(RealTelemetryCollectorState& state) {
    if (!state.gpu_.selectedAdapter.has_value()) {
        state.snapshot_.gpu.name = state.settings_.selection.preferredGpuAdapterName.empty() ? "GPU" :
            state.settings_.selection.preferredGpuAdapterName;
        state.snapshot_.gpu.vram.totalGb = 0.0;
        state.trace_.Write(TracePrefix::Telemetry, RES_STR("gpu_adapter_selected none"));
        return;
    }

    const GpuAdapterInfo& adapter = *state.gpu_.selectedAdapter;
    state.snapshot_.gpu.name = adapter.adapterName.empty() ? "GPU" : adapter.adapterName;
    state.snapshot_.gpu.vram.totalGb =
        static_cast<double>(adapter.dedicatedVideoMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR(
            "gpu_adapter_selected index=%u vendor_id=0x%04X dedicated_bytes=%llu dedicated_gb=%.2f "
            "luid=0x%08x:0x%08x name=\"%s\""
        ),
        adapter.adapterIndex,
        adapter.vendorId,
        static_cast<unsigned long long>(adapter.dedicatedVideoMemoryBytes),
        state.snapshot_.gpu.vram.totalGb,
        static_cast<unsigned int>(adapter.adapterLuidHighPart),
        static_cast<unsigned int>(adapter.adapterLuidLowPart),
        adapter.adapterName.c_str()
    );
}

void ResolveGpuSelection(RealTelemetryCollectorState& state) {
    GpuAdapterSelection selection =
        ResolveGpuAdapterSelection(state.trace_, state.settings_.selection.preferredGpuAdapterName);
    state.gpu_.selectedAdapter = selection.selectedAdapter;
    state.gpu_.adapterCandidates = std::move(selection.candidates);
    state.resolvedSelections_.gpuAdapterName =
        state.gpu_.selectedAdapter.has_value() ? GpuAdapterSelectionName(*state.gpu_.selectedAdapter) : std::string();
    state.snapshot_.gpu = GpuTelemetry{};
    ApplySelectedGpuAdapterInfo(state);
}

void InitializeGpuVendorProvider(RealTelemetryCollectorState& state) {
    state.gpu_.provider =
        CreateGpuVendorTelemetryProvider(state.trace_, state.gpu_.selectedAdapter, state.settings_.collectPresentedFps);
    if (state.gpu_.provider == nullptr) {
        state.trace_.Write(TracePrefix::Telemetry, RES_STR("gpu_provider_create result=null"));
        return;
    }

    state.trace_.Write(TracePrefix::Telemetry, RES_STR("gpu_provider_initialize_begin"));
    if (state.gpu_.provider->Initialize()) {
        ApplyGpuVendorSample(state, state.gpu_.provider->Sample());
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_provider_initialize_done provider=%s available=%s diagnostics=\"%s\""),
            state.gpu_.providerName.c_str(),
            Trace::BoolText(state.gpu_.providerAvailable),
            state.gpu_.providerDiagnostics.c_str()
        );
    } else {
        const GpuVendorTelemetrySample sample = state.gpu_.provider->Sample();
        state.gpu_.providerName = sample.providerName.empty() ? "GPU vendor" : sample.providerName;
        state.gpu_.providerDiagnostics = sample.diagnostics.empty() ?
            ResourceStringText(RES_STR("Provider initialization failed.")) : sample.diagnostics;
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_provider_initialize_failed provider=%s diagnostics=\"%s\""),
            state.gpu_.providerName.c_str(),
            state.gpu_.providerDiagnostics.c_str()
        );
        if (!state.settings_.collectPresentedFps) {
            return;
        }
        state.gpu_.fallbackFpsProvider = CreatePresentedFpsProvider(state.trace_, state.gpu_.selectedAdapter);
        if (state.gpu_.fallbackFpsProvider != nullptr) {
            const bool fpsInitialized = state.gpu_.fallbackFpsProvider->Initialize();
            state.trace_.WriteFmt(
                TracePrefix::Telemetry,
                RES_STR("gpu_fps_fallback_initialize initialized=%s"),
                Trace::BoolText(fpsInitialized)
            );
        }
    }
}

void ApplyFallbackFpsSample(RealTelemetryCollectorState& state) {
    if (
        !state.settings_.collectPresentedFps ||
        state.snapshot_.gpu.fps.value.has_value() ||
        state.gpu_.fallbackFpsProvider == nullptr
    ) {
        return;
    }

    const FpsTelemetrySample fpsSample = state.gpu_.fallbackFpsProvider->Sample();
    state.snapshot_.gpu.fps.issue =
        fpsSample.permissionRequired ? ScalarMetricIssue::PermissionRequired : ScalarMetricIssue::None;
    state.snapshot_.gpu.fpsAppName = fpsSample.processName;
    if (fpsSample.fps.has_value()) {
        state.snapshot_.gpu.fps.value = *fpsSample.fps;
        state.snapshot_.gpu.fps.unit = ScalarMetricUnit::Fps;
    }
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("gpu_fps_fallback_sample available=%s value=%s process=\"%s\" diagnostics=\"%s\""),
        Trace::BoolText(fpsSample.fps.has_value()),
        FormatOptionalFps(fpsSample.fps).c_str(),
        fpsSample.processName.c_str(),
        fpsSample.diagnostics.c_str()
    );
}

void ApplyPoweredOffGpuFpsZero(RealTelemetryCollectorState& state) {
    if (!state.settings_.collectPresentedFps || state.snapshot_.gpu.fps.value.has_value()) {
        return;
    }
    const std::optional<double> clockMhz = state.snapshot_.gpu.clock.value;
    if (!clockMhz.has_value() || *clockMhz > 0.0 || state.snapshot_.gpu.loadPercent > 0.0) {
        return;
    }

    state.snapshot_.gpu.fps.value = 0.0;
    state.snapshot_.gpu.fps.unit = ScalarMetricUnit::Fps;
    state.snapshot_.gpu.fps.issue = ScalarMetricIssue::None;
    state.snapshot_.gpu.fpsAppName.clear();
    state.trace_.Write(TracePrefix::Telemetry, RES_STR("gpu_fps_powered_off_zero"));
}

}  // namespace

void InitializeGpuCollector(RealTelemetryCollectorState& state) {
    ResetGpuProviderState(state);
    ResolveGpuSelection(state);
    InitializeGpuVendorProvider(state);

    const PDH_STATUS queryStatus = PdhOpenQueryA(nullptr, 0, &state.gpu_.query);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_open gpu_query status=%ld"),
        static_cast<long>(queryStatus)
    );
    const PDH_STATUS loadStatus =
        AddCounterCompat(state.gpu_.query, "\\GPU Engine(*)\\Utilization Percentage", &state.gpu_.loadCounter);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_add gpu_load path=\"\\\\GPU Engine(*)\\\\Utilization Percentage\" status=%ld"),
        static_cast<long>(loadStatus)
    );
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_collect gpu_query status=%ld"),
        static_cast<long>(collectStatus)
    );

    const PDH_STATUS memoryQueryStatus = PdhOpenQueryA(nullptr, 0, &state.gpu_.memoryQuery);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_open gpu_memory_query status=%ld"),
        static_cast<long>(memoryQueryStatus)
    );
    const PDH_STATUS memoryCounterStatus = AddCounterCompat(
        state.gpu_.memoryQuery,
        "\\GPU Adapter Memory(*)\\Dedicated Usage",
        &state.gpu_.dedicatedCounter
    );
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_add gpu_memory path=\"\\\\GPU Adapter Memory(*)\\\\Dedicated Usage\" status=%ld"),
        static_cast<long>(memoryCounterStatus)
    );
    const PDH_STATUS memoryCollectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("pdh_collect gpu_memory_query status=%ld"),
        static_cast<long>(memoryCollectStatus)
    );
}

void ReconfigureGpuCollector(RealTelemetryCollectorState& state) {
    state.trace_.WriteFmt(
        TracePrefix::Telemetry,
        RES_STR("gpu_provider_shutdown provider=%s"),
        state.gpu_.providerName.c_str()
    );
    state.gpu_.provider.reset();
    state.gpu_.fallbackFpsProvider.reset();
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
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_vendor_sample provider=%s available=%s diagnostics=\"%s\""),
            state.gpu_.providerName.c_str(),
            Trace::BoolText(state.gpu_.providerAvailable),
            state.gpu_.providerDiagnostics.c_str()
        );
    }
    ApplyBoardGpuFanFallback(state);
    ApplyIntelCpuTemperatureFallback(state);
    ApplyFallbackFpsSample(state);

    if (!hasVendorLoad && state.gpu_.query != nullptr) {
        const std::optional<std::string> filterToken = SelectedGpuPdhLuidToken(state);
        const std::string filterLabel = filterToken.value_or(kPdhAllGpuAdaptersFilterLabel);
        const std::string_view instanceFilter =
            filterToken.has_value() ? std::string_view(*filterToken) : std::string_view();
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.query);
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_collect status=%ld"),
            static_cast<long>(collectStatus)
        );
        const CounterArrayTotals loadTotals =
            ReadCounterArrayTotals(state, state.gpu_.loadCounter, instanceFilter, filterLabel.c_str());
        const double load3d = loadTotals.total3d;
        const double loadAll = loadTotals.total;
        state.snapshot_.gpu.loadPercent = ClampFinite(load3d > 0.0 ? load3d : loadAll, 0.0, 100.0);
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_load filter=\"%s\" matched=%lu load3d=value=%.2f loadAll=value=%.2f selected=value=%.2f"),
            filterLabel.c_str(),
            static_cast<unsigned long>(loadTotals.matchedCount),
            load3d,
            loadAll,
            state.snapshot_.gpu.loadPercent
        );
    }
    ApplyPoweredOffGpuFpsZero(state);
    state.retainedHistoryStore_.PushSample(
        state.snapshot_,
        RetainedHistoryKey::GpuLoad,
        state.snapshot_.gpu.loadPercent
    );

    if (!hasVendorVram && state.gpu_.memoryQuery != nullptr) {
        const std::optional<std::string> filterToken = SelectedGpuPdhLuidToken(state);
        const std::string filterLabel = filterToken.value_or(kPdhAllGpuAdaptersFilterLabel);
        const std::string_view instanceFilter =
            filterToken.has_value() ? std::string_view(*filterToken) : std::string_view();
        const PDH_STATUS collectStatus = PdhCollectQueryData(state.gpu_.memoryQuery);
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_memory_collect status=%ld"),
            static_cast<long>(collectStatus)
        );
        const double bytes = SumCounterArray(state, state.gpu_.dedicatedCounter, instanceFilter, filterLabel.c_str());
        state.snapshot_.gpu.vram.usedGb = FiniteNonNegativeOr(bytes / (1024.0 * 1024.0 * 1024.0));
        state.trace_.WriteFmt(
            TracePrefix::Telemetry,
            RES_STR("gpu_memory filter=\"%s\" bytes=value=%.0f used_gb=value=%.2f"),
            filterLabel.c_str(),
            bytes,
            state.snapshot_.gpu.vram.usedGb
        );
    }

    state.retainedHistoryStore_.PushSample(
        state.snapshot_,
        RetainedHistoryKey::GpuTemperature,
        state.snapshot_.gpu.temperature.value.value_or(0.0)
    );
    state.retainedHistoryStore_.PushSample(
        state.snapshot_,
        RetainedHistoryKey::GpuClock,
        state.snapshot_.gpu.clock.value.value_or(0.0)
    );
    state.retainedHistoryStore_.PushSample(
        state.snapshot_,
        RetainedHistoryKey::GpuFan,
        state.snapshot_.gpu.fan.value.value_or(0.0)
    );
    state.retainedHistoryStore_.PushSample(
        state.snapshot_,
        RetainedHistoryKey::GpuFps,
        state.snapshot_.gpu.fps.value.value_or(0.0)
    );
    state.retainedHistoryStore_.PushSample(
        state.snapshot_,
        RetainedHistoryKey::GpuVram,
        state.snapshot_.gpu.vram.usedGb
    );
}
