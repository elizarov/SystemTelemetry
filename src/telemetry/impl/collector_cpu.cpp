#include "telemetry/impl/collector_cpu.h"

#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/collector_support.h"
#include "util/numeric_safety.h"

namespace {

void WriteTelemetryTrace(const RealTelemetryCollectorState& state, const char* text) {
    state.trace_.Write(text);
}

void UpdateMemory(RealTelemetryCollectorState& state) {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const BOOL ok = GlobalMemoryStatusEx(&memory);
    if (ok) {
        state.snapshot_.cpu.memory.totalGb = memory.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        state.snapshot_.cpu.memory.usedGb = (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
    state.trace_.WriteLazy([&] {
        return "telemetry:memory_status ok=" + Trace::BoolText(ok != FALSE) +
               " total_gb=" + Trace::FormatValueDouble("value", state.snapshot_.cpu.memory.totalGb, 2) +
               " used_gb=" + Trace::FormatValueDouble("value", state.snapshot_.cpu.memory.usedGb, 2);
    });
    state.retainedHistoryStore_.PushSample(state.snapshot_, "cpu.ram", state.snapshot_.cpu.memory.usedGb);
}

}  // namespace

void InitializeCpuCollector(RealTelemetryCollectorState& state) {
    if (const std::string cpuName = DetectCpuName(); !cpuName.empty()) {
        state.snapshot_.cpu.name = cpuName;
    }
    state.trace_.Write("telemetry:cpu_name value=\"" + state.snapshot_.cpu.name + "\"");

    const PDH_STATUS queryStatus = PdhOpenQueryW(nullptr, 0, &state.cpu_.query);
    state.trace_.Write(("telemetry:pdh_open cpu_query status=" + PdhStatusCodeString(queryStatus)).c_str());
    const PDH_STATUS loadStatus = AddCounterCompat(
        state.cpu_.query, L"\\Processor Information(_Total)\\% Processor Utility", &state.cpu_.loadCounter);
    state.trace_.Write(
        ("telemetry:pdh_add cpu_load path=\"\\\\Processor Information(_Total)\\\\% Processor Utility\" status=" +
            PdhStatusCodeString(loadStatus))
            .c_str());
    if (state.cpu_.loadCounter == nullptr) {
        const PDH_STATUS fallbackStatus =
            AddCounterCompat(state.cpu_.query, L"\\Processor(_Total)\\% Processor Time", &state.cpu_.loadCounter);
        state.trace_.Write(("telemetry:pdh_add cpu_load_fallback path=\"\\\\Processor(_Total)\\\\% Processor Time\" "
                            "status=" +
                            PdhStatusCodeString(fallbackStatus))
                .c_str());
    }
    const PDH_STATUS frequencyStatus = AddCounterCompat(
        state.cpu_.query, L"\\Processor Information(_Total)\\Processor Frequency", &state.cpu_.frequencyCounter);
    state.trace_.Write(
        ("telemetry:pdh_add cpu_frequency path=\"\\\\Processor Information(_Total)\\\\Processor Frequency\" status=" +
            PdhStatusCodeString(frequencyStatus))
            .c_str());
    const PDH_STATUS collectStatus = PdhCollectQueryData(state.cpu_.query);
    state.trace_.Write(("telemetry:pdh_collect cpu_query status=" + PdhStatusCodeString(collectStatus)).c_str());
}

void UpdateCpuMetrics(RealTelemetryCollectorState& state) {
    if (state.cpu_.query == nullptr) {
        WriteTelemetryTrace(state, "telemetry:cpu_update skipped=no_query");
        UpdateMemory(state);
        return;
    }

    const PDH_STATUS collectStatus = PdhCollectQueryData(state.cpu_.query);
    state.trace_.WriteLazy([&] { return "telemetry:cpu_collect status=" + PdhStatusCodeString(collectStatus); });

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS loadStatus = PDH_INVALID_DATA;
    if (state.cpu_.loadCounter != nullptr &&
        (loadStatus = PdhGetFormattedCounterValue(state.cpu_.loadCounter, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        state.snapshot_.cpu.loadPercent = ClampFinite(value.doubleValue, 0.0, 100.0);
    }
    state.trace_.WriteLazy([&] {
        return "telemetry:cpu_load status=" + PdhStatusCodeString(loadStatus) + " " +
               Trace::FormatValueDouble("value", state.snapshot_.cpu.loadPercent, 2);
    });
    state.retainedHistoryStore_.PushSample(state.snapshot_, "cpu.load", state.snapshot_.cpu.loadPercent);

    PDH_STATUS clockStatus = PDH_INVALID_DATA;
    if (state.cpu_.frequencyCounter != nullptr &&
        (clockStatus = PdhGetFormattedCounterValue(state.cpu_.frequencyCounter, PDH_FMT_DOUBLE, nullptr, &value)) ==
            ERROR_SUCCESS) {
        state.snapshot_.cpu.clock.value = FiniteOptional(value.doubleValue / 1000.0);
        state.snapshot_.cpu.clock.unit = ScalarMetricUnit::Gigahertz;
    }
    state.trace_.WriteLazy([&] {
        return "telemetry:cpu_clock status=" + PdhStatusCodeString(clockStatus) + " value=" +
               (state.snapshot_.cpu.clock.value.has_value() ? FormatScalarMetric(state.snapshot_.cpu.clock, 2)
                                                            : std::string("N/A"));
    });
    state.retainedHistoryStore_.PushSample(state.snapshot_, "cpu.clock", state.snapshot_.cpu.clock.value.value_or(0.0));

    UpdateMemory(state);
}
