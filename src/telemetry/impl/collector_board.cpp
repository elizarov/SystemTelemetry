#include "telemetry/impl/collector_board.h"

#include <algorithm>

#include "telemetry/board/board_vendor.h"
#include "telemetry/impl/collector_state.h"
#include "telemetry/impl/system_info_support.h"
#include "util/numeric_safety.h"
#include "util/resource_strings.h"
#include "util/strings.h"

namespace {

void ApplyBoardVendorSample(RealTelemetryCollectorState& state, const BoardVendorTelemetrySample& sample) {
    state.board_.providerName = sample.providerName.empty() ? "None" : sample.providerName;
    state.board_.providerDiagnostics =
        sample.diagnostics.empty() ? ResourceStringText(RES_STR("(none)")) : sample.diagnostics;
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

bool HasConfiguredSensorName(const std::unordered_map<std::string, std::string>& sensorNames, const std::string& name) {
    const auto it = sensorNames.find(name);
    return it != sensorNames.end() && !it->second.empty();
}

std::optional<std::string> FindAutoBoardSensorName(
    const std::string& logicalName, const std::vector<std::string>& availableSensorNames) {
    const auto findContaining = [&](const std::string& text) -> std::optional<std::string> {
        const auto it = std::find_if(availableSensorNames.begin(), availableSensorNames.end(), [&](const auto& name) {
            return ContainsInsensitive(name, text);
        });
        return it != availableSensorNames.end() ? std::optional<std::string>(*it) : std::nullopt;
    };

    if (EqualsInsensitive(logicalName, "cpu")) {
        return findContaining("cpu");
    }
    if (EqualsInsensitive(logicalName, "gpu")) {
        return findContaining("gpu");
    }
    if (EqualsInsensitive(logicalName, "system")) {
        if (auto match = findContaining("system"); match.has_value()) {
            return match;
        }
        if (auto match = findContaining("sys"); match.has_value()) {
            return match;
        }
        if (auto match = findContaining("motherboard"); match.has_value()) {
            return match;
        }
        return findContaining("board");
    }
    return std::nullopt;
}

bool ResolveAutoBoardSensorBindings(RealTelemetryCollectorState& state) {
    bool resolved = false;
    for (const std::string& name : state.settings_.board.requestedTemperatureNames) {
        if (HasConfiguredSensorName(state.settings_.board.temperatureSensorNames, name)) {
            continue;
        }
        if (auto sensorName = FindAutoBoardSensorName(name, state.board_.availableTemperatureNames);
            sensorName.has_value()) {
            state.settings_.board.temperatureSensorNames[name] = *sensorName;
            state.resolvedSelections_.boardTemperatureSensorNames[name] = *sensorName;
            resolved = true;
        }
    }
    for (const std::string& name : state.settings_.board.requestedFanNames) {
        if (HasConfiguredSensorName(state.settings_.board.fanSensorNames, name)) {
            continue;
        }
        if (auto sensorName = FindAutoBoardSensorName(name, state.board_.availableFanNames); sensorName.has_value()) {
            state.settings_.board.fanSensorNames[name] = *sensorName;
            state.resolvedSelections_.boardFanSensorNames[name] = *sensorName;
            resolved = true;
        } else if (state.board_.availableFanNames.size() == 1) {
            // Some laptop providers expose a single shared fan as just "Fan"; bind it to any requested fan fallback.
            state.settings_.board.fanSensorNames[name] = state.board_.availableFanNames.front();
            state.resolvedSelections_.boardFanSensorNames[name] = state.board_.availableFanNames.front();
            resolved = true;
        }
    }
    return resolved;
}

}  // namespace

void InitializeBoardCollector(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    InitializeRequestedBoardMetrics(state, settings);

    BoardVendorTelemetryProviderOptions providerOptions;
    providerOptions.synchronousSamples = state.synchronousProviderSamples_;
    state.board_.provider = CreateBoardVendorTelemetryProvider(state.trace_, providerOptions);
    if (state.board_.provider != nullptr) {
        state.trace_.Write(TracePrefix::Telemetry, RES_STR("board_provider_initialize_begin"));
        if (state.board_.provider->Initialize(settings)) {
            ApplyBoardVendorSample(state, state.board_.provider->Sample());
            if (ResolveAutoBoardSensorBindings(state)) {
                state.board_.provider->Initialize(state.settings_.board);
                ApplyBoardVendorSample(state, state.board_.provider->Sample());
            }
            state.trace_.WriteFmt(TracePrefix::Telemetry,
                RES_STR("board_provider_initialize_done provider=%s available=%s diagnostics=\"%s\""),
                state.board_.providerName.c_str(),
                Trace::BoolText(state.board_.providerAvailable),
                state.board_.providerDiagnostics.c_str());
        } else {
            ApplyBoardVendorSample(state, state.board_.provider->Sample());
            state.trace_.WriteFmt(TracePrefix::Telemetry,
                RES_STR("board_provider_initialize_failed provider=%s diagnostics=\"%s\""),
                state.board_.providerName.c_str(),
                state.board_.providerDiagnostics.c_str());
        }
    } else {
        state.trace_.Write(TracePrefix::Telemetry, RES_STR("board_provider_create result=null"));
    }
}

void ReconfigureBoardCollector(RealTelemetryCollectorState& state, const BoardTelemetrySettings& settings) {
    InitializeRequestedBoardMetrics(state, settings);

    if (state.board_.provider == nullptr) {
        return;
    }

    state.trace_.Write(TracePrefix::Telemetry, RES_STR("board_provider_reconfigure_begin"));
    if (state.board_.provider->Initialize(settings)) {
        ApplyBoardVendorSample(state, state.board_.provider->Sample());
        if (ResolveAutoBoardSensorBindings(state)) {
            state.board_.provider->Initialize(state.settings_.board);
            ApplyBoardVendorSample(state, state.board_.provider->Sample());
        }
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("board_provider_reconfigure_done provider=%s available=%s diagnostics=\"%s\""),
            state.board_.providerName.c_str(),
            Trace::BoolText(state.board_.providerAvailable),
            state.board_.providerDiagnostics.c_str());
    } else {
        ApplyBoardVendorSample(state, state.board_.provider->Sample());
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("board_provider_reconfigure_failed provider=%s diagnostics=\"%s\""),
            state.board_.providerName.c_str(),
            state.board_.providerDiagnostics.c_str());
    }
}

void UpdateBoardMetrics(RealTelemetryCollectorState& state) {
    if (state.board_.provider != nullptr) {
        ApplyBoardVendorSample(state, state.board_.provider->Sample());
        if (ResolveAutoBoardSensorBindings(state)) {
            state.board_.provider->Initialize(state.settings_.board);
            ApplyBoardVendorSample(state, state.board_.provider->Sample());
        }
        state.trace_.WriteFmt(TracePrefix::Telemetry,
            RES_STR("board_vendor_sample provider=%s available=%s diagnostics=\"%s\""),
            state.board_.providerName.c_str(),
            Trace::BoolText(state.board_.providerAvailable),
            state.board_.providerDiagnostics.c_str());
    }
    state.retainedHistoryStore_.PushBoardMetricSamples(state.snapshot_);
}
