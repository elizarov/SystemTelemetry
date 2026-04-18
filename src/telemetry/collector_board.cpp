#include "telemetry/collector_board.h"

#include "board_vendor.h"
#include "numeric_safety.h"
#include "system_info_support.h"
#include "telemetry/collector_state.h"

namespace {

void ApplyBoardVendorSample(RealTelemetryCollectorState& state, const BoardVendorTelemetrySample& sample) {
    state.board_.providerName = sample.providerName.empty() ? "None" : sample.providerName;
    state.board_.providerDiagnostics = sample.diagnostics.empty() ? "(none)" : sample.diagnostics;
    state.board_.providerAvailable = sample.available;
    state.board_.boardManufacturer = sample.boardManufacturer;
    state.board_.boardProduct = sample.boardProduct;
    state.board_.driverLibrary = sample.driverLibrary;
    state.board_.requestedFanNames = sample.requestedFanNames;
    state.board_.requestedTemperatureNames = sample.requestedTemperatureNames;
    UpdateDiscoveredBoardSensorNames(state.board_.availableFanNames, sample.availableFanNames);
    UpdateDiscoveredBoardSensorNames(state.board_.availableTemperatureNames, sample.availableTemperatureNames);
    state.snapshot_.boardTemperatures = sample.temperatures;
    for (auto& metric : state.snapshot_.boardTemperatures) {
        metric.metric.value = FiniteOptional(metric.metric.value);
    }
    state.snapshot_.boardFans = sample.fans;
    for (auto& metric : state.snapshot_.boardFans) {
        metric.metric.value = FiniteOptional(metric.metric.value);
    }
}

void InitializeRequestedBoardMetrics(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    state.snapshot_.boardTemperatures =
        CreateRequestedBoardMetrics(settings.requestedTemperatureNames, ScalarMetricUnit::Celsius);
    state.snapshot_.boardFans = CreateRequestedBoardMetrics(settings.requestedFanNames, ScalarMetricUnit::Rpm);
}

}  // namespace

void InitializeBoardCollector(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    InitializeRequestedBoardMetrics(state, settings);

    state.board_.provider = CreateBoardVendorTelemetryProvider(&state.trace_);
    if (state.board_.provider != nullptr) {
        state.trace_.Write("telemetry:board_provider_initialize_begin");
        if (state.board_.provider->Initialize(settings)) {
            ApplyBoardVendorSample(state, state.board_.provider->Sample());
            state.trace_.Write("telemetry:board_provider_initialize_done provider=" + state.board_.providerName +
                               " available=" + tracing::Trace::BoolText(state.board_.providerAvailable) +
                               " diagnostics=\"" + state.board_.providerDiagnostics + "\"");
        } else {
            ApplyBoardVendorSample(state, state.board_.provider->Sample());
            state.trace_.Write("telemetry:board_provider_initialize_failed provider=" + state.board_.providerName +
                               " diagnostics=\"" + state.board_.providerDiagnostics + "\"");
        }
    } else {
        state.trace_.Write("telemetry:board_provider_create result=null");
    }
}

void ReconfigureBoardCollector(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    InitializeRequestedBoardMetrics(state, settings);

    if (state.board_.provider == nullptr) {
        return;
    }

    state.trace_.Write("telemetry:board_provider_reconfigure_begin");
    if (state.board_.provider->Initialize(settings)) {
        ApplyBoardVendorSample(state, state.board_.provider->Sample());
        state.trace_.Write("telemetry:board_provider_reconfigure_done provider=" + state.board_.providerName +
                           " available=" + tracing::Trace::BoolText(state.board_.providerAvailable) +
                           " diagnostics=\"" + state.board_.providerDiagnostics + "\"");
    } else {
        ApplyBoardVendorSample(state, state.board_.provider->Sample());
        state.trace_.Write("telemetry:board_provider_reconfigure_failed provider=" + state.board_.providerName +
                           " diagnostics=\"" + state.board_.providerDiagnostics + "\"");
    }
}

void UpdateBoardMetrics(RealTelemetryCollectorState& state) {
    if (state.board_.provider != nullptr) {
        ApplyBoardVendorSample(state, state.board_.provider->Sample());
        state.trace_.WriteLazy([&] {
            return "telemetry:board_vendor_sample provider=" + state.board_.providerName +
                   " available=" + tracing::Trace::BoolText(state.board_.providerAvailable) + " diagnostics=\"" +
                   state.board_.providerDiagnostics + "\"";
        });
    }
    state.retainedHistoryStore_.PushBoardMetricSamples(state.snapshot_);
}
