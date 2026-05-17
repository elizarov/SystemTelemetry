#include "telemetry/impl/collector_cpu.h"

#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"

namespace {

void WriteTelemetryTrace(const RealTelemetryCollectorState& state, const char* text) {
    state.trace_.Write(TracePrefix::Telemetry, text);
}

void UpdateMemory(RealTelemetryCollectorState& state) {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const BOOL ok = GlobalMemoryStatusEx(&memory);
    if (ok) {
        state.snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        state.snapshot_.cpu.memory.usedGb = (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("memory_status ok=%s total_gb=value=%.2f used_gb=value=%.2f"),
        Trace::BoolText(ok != FALSE),
        state.snapshot_.cpu.memory.totalGb,
        state.snapshot_.cpu.memory.usedGb);
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::CpuRam, state.snapshot_.cpu.memory.usedGb);
}

}  // namespace

void InitializeCpuCollector(RealTelemetryCollectorState& state) {
    if (const std::string cpuName = DetectCpuName(); !cpuName.empty()) {
        state.snapshot_.cpu.name = cpuName;
    }
    state.trace_.WriteFmt(TracePrefix::Telemetry, RES_STR("cpu_name value=\"%s\""), state.snapshot_.cpu.name.c_str());

    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.cpu_.query);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry, RES_STR("pdh_open cpu_query status=%ld"), static_cast<long>(queryStatus));
    const PDH_STATUS loadStatus = AddCounterCompat(
        state.cpu_.query, "\\Processor Information(_Total)\\% Processor Utility", &state.cpu_.loadCounter);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("pdh_add cpu_load path=\"\\\\Processor Information(_Total)\\\\%% Processor Utility\" status=%ld"),
        static_cast<long>(loadStatus));
    if (state.cpu_.loadCounter == nullptr) {
        const PDH_STATUS fallbackStatus =
            AddCounterCompat(state.cpu_.query, "\\Processor(_Total)\\% Processor Time", &state.cpu_.loadCounter);
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("pdh_add cpu_load_fallback path=\"\\\\Processor(_Total)\\\\%% Processor Time\" status=%ld"),
            static_cast<long>(fallbackStatus));
    }
    const PDH_STATUS frequencyStatus = AddCounterCompat(
        state.cpu_.query, "\\Processor Information(_Total)\\Processor Frequency", &state.cpu_.frequencyCounter);
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("pdh_add cpu_frequency path=\"\\\\Processor Information(_Total)\\\\Processor Frequency\" status=%ld"),
        static_cast<long>(frequencyStatus));
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.cpu_.query);
    state.trace_.WriteFmt(
        TracePrefix::Telemetry, RES_STR("pdh_collect cpu_query status=%ld"), static_cast<long>(collectStatus));
}

void UpdateCpuMetrics(RealTelemetryCollectorState& state) {
    if (state.cpu_.query == nullptr) {
        WriteTelemetryTrace(state, "cpu_update skipped=no_query");
        UpdateMemory(state);
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(state.cpu_.query);
    state.trace_.WriteFmt(TracePrefix::Telemetry, RES_STR("cpu_collect status=%ld"), static_cast<long>(collectStatus));

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS loadStatus = PDH_INVALID_DATA;
    if (state.cpu_.loadCounter != nullptr) {
        loadStatus = PdhGetFormattedCounterValue(state.cpu_.loadCounter, PDH_FMT_DOUBLE, nullptr, &value);
        if (loadStatus == ERROR_SUCCESS) {
            state.snapshot_.cpu.loadPercent = ClampFinite(value.doubleValue, 0.0, 100.0);
        }
    }
    state.trace_.WriteFmt(TracePrefix::Telemetry,
        RES_STR("cpu_load status=%ld value=%.2f"),
        static_cast<long>(loadStatus),
        state.snapshot_.cpu.loadPercent);
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::CpuLoad, state.snapshot_.cpu.loadPercent);

    PDH_STATUS clockStatus = PDH_INVALID_DATA;
    if (state.cpu_.frequencyCounter != nullptr) {
        clockStatus = PdhGetFormattedCounterValue(state.cpu_.frequencyCounter, PDH_FMT_DOUBLE, nullptr, &value);
        if (clockStatus == ERROR_SUCCESS) {
            state.snapshot_.cpu.clock.value = FiniteOptional(value.doubleValue / 1000.0);
            state.snapshot_.cpu.clock.unit = ScalarMetricUnit::Gigahertz;
        }
    }
    if (state.trace_.Enabled(TracePrefix::Telemetry)) {
        const std::string valueText =
            state.snapshot_.cpu.clock.value.has_value() ? FormatScalarMetric(state.snapshot_.cpu.clock, 2) : "N/A";
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("cpu_clock status=%ld value=%s"),
            static_cast<long>(clockStatus),
            valueText.c_str());
    }
    state.retainedHistoryStore_.PushSample(
        state.snapshot_, RetainedHistoryKey::CpuClock, state.snapshot_.cpu.clock.value.value_or(0.0));

    UpdateMemory(state);
}
