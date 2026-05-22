#include "telemetry/metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include "config/config_telemetry.h"
#include "telemetry/timing.h"
#include "util/numeric_safety.h"
#include "util/text_format.h"

namespace {

constexpr char kBoardTemperaturePrefix[] = "board.temp.";
constexpr char kBoardFanPrefix[] = "board.fan.";
constexpr char kPermissionRequiredText[] = "!admin";

enum class MetricPayloadKind {
    Text,
    Value,
    Throughput,
};

enum class ThroughputGraphGroup : std::uint8_t {
    None,
    Network,
    Storage,
};

enum class MetricBindingKind : std::uint8_t {
    CpuName,
    GpuName,
    Nothing,
    CpuLoad,
    CpuClock,
    CpuMemory,
    GpuLoad,
    GpuTemperature,
    GpuClock,
    GpuFan,
    GpuFps,
    GpuMemory,
    NetworkUpload,
    NetworkDownload,
    StorageRead,
    StorageWrite,
    DriveActivityRead,
    DriveActivityWrite,
    DriveUsage,
    DriveFree,
    BoardTemperature,
    BoardFan,
};

enum MetricBindingFlags : std::uint8_t {
    kPrefixMatchFlag = 1u << 0,
    kHasMetricStyleFlag = 1u << 1,
    kGenerallyAvailableFlag = 1u << 2,
    kStaticTextFlag = 1u << 3,
    kTextPayloadFlag = 1u << 4,
    kValuePayloadFlag = 1u << 5,
    kThroughputPayloadFlag = 1u << 6,
};

struct MetricBinding {
    const char* key = "";
    MetricDisplayStyle metricStyle = MetricDisplayStyle::Scalar;
    MetricBindingKind kind = MetricBindingKind::Nothing;
    ThroughputGraphGroup throughputGroup = ThroughputGraphGroup::None;
    std::uint8_t flags = kGenerallyAvailableFlag;
};

static_assert(sizeof(MetricBinding) == 16);

struct MetricBindingMatch {
    const MetricBinding* binding = nullptr;
    std::string_view logicalName;
};

constexpr std::uint8_t operator|(MetricBindingFlags lhs, MetricBindingFlags rhs) {
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr std::uint8_t operator|(std::uint8_t lhs, MetricBindingFlags rhs) {
    return static_cast<std::uint8_t>(lhs | static_cast<std::uint8_t>(rhs));
}

bool BindingHasFlag(const MetricBinding& binding, MetricBindingFlags flag) {
    return (binding.flags & static_cast<std::uint8_t>(flag)) != 0;
}

bool BindingSupportsPayload(const MetricBinding& binding, MetricPayloadKind kind) {
    switch (kind) {
        case MetricPayloadKind::Text:
            return BindingHasFlag(binding, kTextPayloadFlag);
        case MetricPayloadKind::Value:
            return BindingHasFlag(binding, kValuePayloadFlag);
        case MetricPayloadKind::Throughput:
            return BindingHasFlag(binding, kThroughputPayloadFlag);
    }
    return false;
}

std::string FormatScalarValue(std::optional<double> value, std::string_view unit, int precision) {
    if (!value.has_value() || !IsFiniteDouble(*value)) {
        return "N/A";
    }
    if (unit.empty()) {
        return FormatText("%.*f", precision, *value);
    }
    return FormatText("%.*f %.*s", precision, *value, static_cast<int>(unit.size()), unit.data());
}

std::string FormatPercentValue(std::optional<double> value, std::string_view unit, int precision) {
    if (!value.has_value() || !IsFiniteDouble(*value)) {
        return "N/A";
    }
    if (unit.empty()) {
        return FormatText("%.*f", precision, *value);
    } else if (unit == "%") {
        return FormatText("%.*f%%", precision, *value);
    }
    return FormatText("%.*f %.*s", precision, *value, static_cast<int>(unit.size()), unit.data());
}

std::string FormatMemoryValue(double usedGb, double totalGb, std::string_view unit) {
    if (!IsFiniteDouble(usedGb) || !IsFiniteDouble(totalGb) || totalGb <= 0.0) {
        return "N/A";
    }
    if (unit.empty()) {
        return FormatText("%.1f / %.0f", usedGb, totalGb);
    }
    return FormatText("%.1f / %.0f %.*s", usedGb, totalGb, static_cast<int>(unit.size()), unit.data());
}

std::string FormatThroughputValue(double valueMbps, std::string_view unit) {
    if (!IsFiniteDouble(valueMbps) || valueMbps < 0.0) {
        return "N/A";
    }
    if (unit.empty()) {
        return FormatText(valueMbps >= 100.0 ? "%.0f" : "%.1f", valueMbps);
    }
    return FormatText(
        valueMbps >= 100.0 ? "%.0f %.*s" : "%.1f %.*s", valueMbps, static_cast<int>(unit.size()), unit.data());
}

std::pair<std::string_view, std::string_view> SplitSizeUnits(std::string_view units) {
    const size_t separator = units.find('|');
    if (separator == std::string_view::npos) {
        return {units, units};
    }
    return {units.substr(0, separator), units.substr(separator + 1)};
}

std::string FormatSizeAutoValue(double valueGb, std::string_view units) {
    if (!IsFiniteDouble(valueGb) || valueGb < 0.0) {
        return "N/A";
    }
    const auto [smallUnit, largeUnit] = SplitSizeUnits(units);
    if (valueGb >= 1024.0) {
        if (largeUnit.empty()) {
            return FormatText("%.1f", valueGb / 1024.0);
        }
        return FormatText("%.1f %.*s", valueGb / 1024.0, static_cast<int>(largeUnit.size()), largeUnit.data());
    } else if (smallUnit.empty()) {
        return FormatText("%.0f", valueGb);
    }
    return FormatText("%.0f %.*s", valueGb, static_cast<int>(smallUnit.size()), smallUnit.data());
}

double AverageThroughputLiveSamples(const std::vector<double>& samples) {
    if (samples.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (double sample : samples) {
        total += FiniteNonNegativeOr(sample);
    }
    return total / static_cast<double>(samples.size());
}

double ResolveThroughputLiveLeader(const RetainedHistorySeries* history, double fallbackValue) {
    if (history == nullptr) {
        return FiniteNonNegativeOr(fallbackValue);
    }
    if (!history->throughputLiveSamples.empty()) {
        return AverageThroughputLiveSamples(history->throughputLiveSamples);
    }
    if (!history->samples.empty()) {
        return FiniteNonNegativeOr(history->samples.back());
    }
    return FiniteNonNegativeOr(fallbackValue);
}

double ResolveThroughputPlotShift(const RetainedHistorySeries* history) {
    if (history == nullptr || history->throughputBucketSampleCount == 0) {
        return 0.0;
    }
    return std::clamp(static_cast<double>(history->throughputBucketSampleCount) /
            static_cast<double>(kThroughputHistorySmoothingSamples),
        0.0,
        1.0);
}

double GetThroughputGraphMax(
    const MetricSource::ThroughputSharedState::HistoryEntry* const* histories, size_t historyCount) {
    double maxDisplayedValue = 10.0;
    for (size_t i = 0; i < historyCount; ++i) {
        const auto* history = histories[i];
        if (history == nullptr) {
            continue;
        }
        for (double value : history->samples) {
            maxDisplayedValue = std::max(maxDisplayedValue, FiniteNonNegativeOr(value));
        }
        maxDisplayedValue = std::max(maxDisplayedValue, FiniteNonNegativeOr(history->liveLeaderMbps));
    }
    const double roundingStep = maxDisplayedValue > 100.0 ? 50.0 : 5.0;
    return std::max(10.0, std::ceil(maxDisplayedValue / roundingStep) * roundingStep);
}

double GetThroughputGuideStep(double maxGraph) {
    return maxGraph > 50.0 ? 50.0 : 5.0;
}

double GetTimeMarkerOffsetSamples(const SYSTEMTIME& now) {
    const double secondsIntoTenSecondWindow = std::fmod(
        static_cast<double>(now.wSecond) + (static_cast<double>(now.wMilliseconds) / 1000.0),
        kThroughputTimeMarkerIntervalSeconds);
    return secondsIntoTenSecondWindow / kThroughputHistoryPointSeconds;
}

double ResolveMetricRatio(const MetricDefinitionConfig& definition, double value, double telemetryScale = 0.0) {
    const double scale = definition.telemetryScale ? telemetryScale : definition.scale;
    if (!IsFiniteDouble(value) || !IsFiniteDouble(scale) || scale <= 0.0) {
        return 0.0;
    }
    return ClampFinite(value / scale, 0.0, 1.0);
}

int ResolveScalarPrecision(const std::string& metricRef) {
    if (metricRef == "cpu.clock") {
        return 2;
    }
    return 0;
}

const RetainedHistorySeries* FindRetainedHistorySeries(const SystemSnapshot& snapshot, const std::string& seriesRef) {
    RetainedHistoryKey key = RetainedHistoryKey::Count;
    if (TryRetainedHistoryKey(seriesRef, key)) {
        const uint16_t encodedIndex = snapshot.retainedHistoryIndexByKey[static_cast<size_t>(key)];
        if (encodedIndex != 0) {
            const size_t index = encodedIndex - 1u;
            if (index < snapshot.retainedHistories.size() && snapshot.retainedHistories[index].seriesRef == seriesRef) {
                return &snapshot.retainedHistories[index];
            }
        }
    }
    for (const auto& history : snapshot.retainedHistories) {
        if (history.seriesRef == seriesRef) {
            return &history;
        }
    }
    return nullptr;
}

const std::vector<double>* FindRetainedHistory(const SystemSnapshot& snapshot, const std::string& seriesRef) {
    const auto* history = FindRetainedHistorySeries(snapshot, seriesRef);
    return history != nullptr ? &history->samples : nullptr;
}

double ResolvePeakRatio(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    double fallbackRatio,
    double telemetryScale = 0.0) {
    const auto* history = FindRetainedHistory(snapshot, metricRef);
    if (history == nullptr || history->empty()) {
        return ClampFinite(fallbackRatio, 0.0, 1.0);
    }
    double peak = 0.0;
    for (double value : *history) {
        peak = std::max(peak, ResolveMetricRatio(definition, value, telemetryScale));
    }
    return ClampFinite(peak, 0.0, 1.0);
}

void ResolveRetainedThroughputHistory(const SystemSnapshot& snapshot,
    const MetricBinding& binding,
    double fallbackValue,
    std::vector<double>& samples,
    double& liveLeaderMbps,
    double& plotShiftSamples) {
    const auto* history = FindRetainedHistorySeries(snapshot, binding.key);
    samples = history != nullptr ? history->samples : std::vector<double>{};
    liveLeaderMbps = ResolveThroughputLiveLeader(history, fallbackValue);
    plotShiftSamples = ResolveThroughputPlotShift(history);
}

std::string FormatMetricValueText(const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::optional<double> primaryValue,
    std::optional<double> secondaryValue = std::nullopt) {
    switch (definition.style) {
        case MetricDisplayStyle::Percent:
            return FormatPercentValue(primaryValue, definition.unit, 0);
        case MetricDisplayStyle::Scalar:
            return FormatScalarValue(primaryValue, definition.unit, ResolveScalarPrecision(metricRef));
        case MetricDisplayStyle::Memory:
            return primaryValue.has_value() && secondaryValue.has_value() ?
                FormatMemoryValue(*primaryValue, *secondaryValue, definition.unit) :
                std::string("N/A");
        case MetricDisplayStyle::Throughput:
            return primaryValue.has_value() ? FormatThroughputValue(*primaryValue, definition.unit) :
                std::string("N/A");
        case MetricDisplayStyle::SizeAuto:
            return primaryValue.has_value() ? FormatSizeAutoValue(*primaryValue, definition.unit) : std::string("N/A");
        case MetricDisplayStyle::LabelOnly:
            return {};
    }
    return "N/A";
}

std::string BuildMetricSampleValueText(const MetricDefinitionConfig& definition, const std::string& metricRef) {
    switch (definition.style) {
        case MetricDisplayStyle::Percent:
            return FormatPercentValue(
                std::optional<double>{definition.telemetryScale ? 100.0 : definition.scale}, definition.unit, 0);
        case MetricDisplayStyle::Scalar:
            return FormatScalarValue(std::optional<double>{definition.telemetryScale ? 100.0 : definition.scale},
                definition.unit,
                ResolveScalarPrecision(metricRef));
        case MetricDisplayStyle::Memory:
            return FormatMemoryValue(999.9, 1000.0, definition.unit);
        case MetricDisplayStyle::Throughput:
            return FormatThroughputValue(999.9, definition.unit);
        case MetricDisplayStyle::SizeAuto:
            return FormatSizeAutoValue(2048.0, definition.unit);
        case MetricDisplayStyle::LabelOnly:
            return {};
    }
    return {};
}

MetricValueState InferMetricValueState(std::string_view valueText) {
    return valueText.empty() || valueText == "N/A" ? MetricValueState::Unavailable : MetricValueState::Available;
}

MetricValue BuildResolvedMetric(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::string valueText,
    double ratio,
    double telemetryScale = 0.0,
    MetricValueState state = MetricValueState::Available,
    std::string annotationText = {},
    bool warningAnnotation = false) {
    if (state == MetricValueState::Available) {
        state = InferMetricValueState(valueText);
    }
    return MetricValue{definition.label,
        std::move(valueText),
        std::move(annotationText),
        BuildMetricSampleValueText(definition, metricRef),
        definition.unit,
        ratio,
        ResolvePeakRatio(snapshot, definition, metricRef, ratio, telemetryScale),
        state,
        warningAnnotation};
}

MetricValue ResolveBoardMetric(const std::vector<NamedScalarMetric>& metrics,
    const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::string_view logicalName) {
    for (const auto& metric : metrics) {
        if (metric.name != logicalName) {
            continue;
        }
        const double numericValue = FiniteNonNegativeOr(metric.metric.value.value_or(0.0));
        const double ratio = ResolveMetricRatio(definition, numericValue);
        return BuildResolvedMetric(
            snapshot, definition, metricRef, FormatMetricValueText(definition, metricRef, metric.metric.value), ratio);
    }

    return BuildResolvedMetric(snapshot, definition, metricRef, "N/A", 0.0);
}

MetricValue ResolvePercentMetric(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    double rawPercent) {
    const double percent = ClampFinite(rawPercent, 0.0, 100.0);
    const double ratio = ResolveMetricRatio(definition, percent, 100.0);
    return BuildResolvedMetric(
        snapshot, definition, metricRef, FormatMetricValueText(definition, metricRef, percent), ratio, 100.0);
}

MetricValue ResolveOptionalScalarMetric(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::optional<double> metricValue) {
    const double value = FiniteNonNegativeOr(metricValue.value_or(0.0));
    const double ratio = ResolveMetricRatio(definition, value);
    return BuildResolvedMetric(
        snapshot, definition, metricRef, FormatMetricValueText(definition, metricRef, metricValue), ratio);
}

MetricValue ResolveMemoryMetric(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    const MemoryMetric& memory) {
    const double total = FiniteNonNegativeOr(memory.totalGb);
    const double used = FiniteNonNegativeOr(memory.usedGb);
    const double ratio = ResolveMetricRatio(definition, used, total);
    return BuildResolvedMetric(snapshot,
        definition,
        metricRef,
        FormatMetricValueText(definition, metricRef, memory.usedGb, memory.totalGb),
        ratio,
        total);
}

MetricValue ResolveGpuFpsMetric(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    std::string_view) {
    const bool permissionRequired = snapshot.gpu.fps.issue == ScalarMetricIssue::PermissionRequired;
    if (!snapshot.gpu.fps.value.has_value() && permissionRequired) {
        return BuildResolvedMetric(snapshot,
            definition,
            metricRef,
            std::string(kPermissionRequiredText),
            0.0,
            0.0,
            MetricValueState::PermissionRequired);
    }

    if (!snapshot.gpu.fps.value.has_value()) {
        return BuildResolvedMetric(
            snapshot, definition, metricRef, "N/A", 0.0, 0.0, MetricValueState::Unavailable, snapshot.gpu.fpsAppName);
    }

    const double value = FiniteNonNegativeOr(snapshot.gpu.fps.value.value_or(0.0));
    const double ratio = ResolveMetricRatio(definition, value);
    return BuildResolvedMetric(snapshot,
        definition,
        metricRef,
        FormatMetricValueText(definition, metricRef, snapshot.gpu.fps.value),
        ratio,
        0.0,
        MetricValueState::Available,
        permissionRequired ? std::string(kPermissionRequiredText) : snapshot.gpu.fpsAppName,
        permissionRequired);
}

MetricValue ResolveMetricByKind(const SystemSnapshot& snapshot,
    const MetricDefinitionConfig& definition,
    const std::string& metricRef,
    MetricBindingKind kind,
    std::string_view logicalName) {
    switch (kind) {
        case MetricBindingKind::Nothing:
            return BuildResolvedMetric(snapshot, definition, metricRef, "N/A", 0.0);
        case MetricBindingKind::CpuLoad:
            return ResolvePercentMetric(snapshot, definition, metricRef, snapshot.cpu.loadPercent);
        case MetricBindingKind::CpuClock:
            return ResolveOptionalScalarMetric(snapshot, definition, metricRef, snapshot.cpu.clock.value);
        case MetricBindingKind::CpuMemory:
            return ResolveMemoryMetric(snapshot, definition, metricRef, snapshot.cpu.memory);
        case MetricBindingKind::GpuLoad:
            return ResolvePercentMetric(snapshot, definition, metricRef, snapshot.gpu.loadPercent);
        case MetricBindingKind::GpuTemperature:
            return ResolveOptionalScalarMetric(snapshot, definition, metricRef, snapshot.gpu.temperature.value);
        case MetricBindingKind::GpuClock:
            return ResolveOptionalScalarMetric(snapshot, definition, metricRef, snapshot.gpu.clock.value);
        case MetricBindingKind::GpuFan:
            return ResolveOptionalScalarMetric(snapshot, definition, metricRef, snapshot.gpu.fan.value);
        case MetricBindingKind::GpuFps:
            return ResolveGpuFpsMetric(snapshot, definition, metricRef, logicalName);
        case MetricBindingKind::GpuMemory:
            return ResolveMemoryMetric(snapshot, definition, metricRef, snapshot.gpu.vram);
        case MetricBindingKind::BoardTemperature:
            return ResolveBoardMetric(snapshot.boardTemperatures, snapshot, definition, metricRef, logicalName);
        case MetricBindingKind::BoardFan:
            return ResolveBoardMetric(snapshot.boardFans, snapshot, definition, metricRef, logicalName);
        default:
            return {};
    }
}

std::string ResolveTextByKind(const SystemSnapshot& snapshot, MetricBindingKind kind) {
    switch (kind) {
        case MetricBindingKind::CpuName:
            return snapshot.cpu.name;
        case MetricBindingKind::GpuName:
            return snapshot.gpu.name;
        default:
            return {};
    }
}

const MetricBinding kExactBindings[] = {
    {"cpu.name",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::CpuName,
        ThroughputGraphGroup::None,
        kTextPayloadFlag | kStaticTextFlag},
    {"gpu.name",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::GpuName,
        ThroughputGraphGroup::None,
        kTextPayloadFlag | kStaticTextFlag},
    {"nothing",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::Nothing,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"cpu.load",
        MetricDisplayStyle::Percent,
        MetricBindingKind::CpuLoad,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"cpu.clock",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::CpuClock,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"cpu.ram",
        MetricDisplayStyle::Memory,
        MetricBindingKind::CpuMemory,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"gpu.load",
        MetricDisplayStyle::Percent,
        MetricBindingKind::GpuLoad,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"gpu.temp",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::GpuTemperature,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"gpu.clock",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::GpuClock,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"gpu.fan",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::GpuFan,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"gpu.fps",
        MetricDisplayStyle::Scalar,
        MetricBindingKind::GpuFps,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"gpu.vram",
        MetricDisplayStyle::Memory,
        MetricBindingKind::GpuMemory,
        ThroughputGraphGroup::None,
        kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"network.upload",
        MetricDisplayStyle::Throughput,
        MetricBindingKind::NetworkUpload,
        ThroughputGraphGroup::Network,
        kThroughputPayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"network.download",
        MetricDisplayStyle::Throughput,
        MetricBindingKind::NetworkDownload,
        ThroughputGraphGroup::Network,
        kThroughputPayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"storage.read",
        MetricDisplayStyle::Throughput,
        MetricBindingKind::StorageRead,
        ThroughputGraphGroup::Storage,
        kThroughputPayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"storage.write",
        MetricDisplayStyle::Throughput,
        MetricBindingKind::StorageWrite,
        ThroughputGraphGroup::Storage,
        kThroughputPayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {"drive.activity.read",
        MetricDisplayStyle::LabelOnly,
        MetricBindingKind::DriveActivityRead,
        ThroughputGraphGroup::None,
        kHasMetricStyleFlag},
    {"drive.activity.write",
        MetricDisplayStyle::LabelOnly,
        MetricBindingKind::DriveActivityWrite,
        ThroughputGraphGroup::None,
        kHasMetricStyleFlag},
    {"drive.usage",
        MetricDisplayStyle::Percent,
        MetricBindingKind::DriveUsage,
        ThroughputGraphGroup::None,
        kHasMetricStyleFlag},
    {"drive.free",
        MetricDisplayStyle::SizeAuto,
        MetricBindingKind::DriveFree,
        ThroughputGraphGroup::None,
        kHasMetricStyleFlag},
};

const MetricBinding kPrefixBindings[] = {
    {kBoardTemperaturePrefix,
        MetricDisplayStyle::Scalar,
        MetricBindingKind::BoardTemperature,
        ThroughputGraphGroup::None,
        kPrefixMatchFlag | kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
    {kBoardFanPrefix,
        MetricDisplayStyle::Scalar,
        MetricBindingKind::BoardFan,
        ThroughputGraphGroup::None,
        kPrefixMatchFlag | kValuePayloadFlag | kHasMetricStyleFlag | kGenerallyAvailableFlag},
};

MetricBindingMatch FindMetricBinding(std::string_view metricRef) {
    for (const auto& binding : kExactBindings) {
        if (std::string_view(binding.key) == metricRef) {
            return MetricBindingMatch{&binding, {}};
        }
    }
    for (const auto& binding : kPrefixBindings) {
        const std::string_view key(binding.key);
        if (metricRef.rfind(key, 0) == 0) {
            return MetricBindingMatch{&binding, metricRef.substr(key.size())};
        }
    }
    return {};
}

bool ResolveMetricValue(const SystemSnapshot& snapshot,
    const MetricsSectionConfig& metrics,
    const std::string& metricRef,
    MetricValue& value) {
    const MetricBindingMatch match = FindMetricBinding(metricRef);
    if (match.binding == nullptr || !BindingSupportsPayload(*match.binding, MetricPayloadKind::Value)) {
        return false;
    }

    const MetricDefinitionConfig* definition = FindEffectiveMetricDefinition(metrics, metricRef);
    if (definition == nullptr) {
        return false;
    }
    value = ResolveMetricByKind(snapshot, *definition, metricRef, match.binding->kind, match.logicalName);
    return true;
}

const MetricSource::ThroughputSharedState::HistoryEntry* FindThroughputHistory(
    const MetricSource::ThroughputSharedState& state, std::string_view metricRef) {
    for (size_t i = 0; i < state.historyCount; ++i) {
        const auto& entry = state.histories[i];
        if (entry.metricRef != nullptr && std::string_view(entry.metricRef) == metricRef) {
            return &entry;
        }
    }
    return nullptr;
}

double ResolveThroughputGraphMax(const MetricSource::ThroughputSharedState& state, const MetricBinding& binding) {
    switch (binding.throughputGroup) {
        case ThroughputGraphGroup::Network:
            return state.networkMaxGraph;
        case ThroughputGraphGroup::Storage:
            return state.storageMaxGraph;
        case ThroughputGraphGroup::None:
            return 10.0;
    }
    return 10.0;
}

double ResolveThroughputValue(const SystemSnapshot& snapshot, MetricBindingKind kind) {
    switch (kind) {
        case MetricBindingKind::NetworkUpload:
            return snapshot.network.uploadMbps;
        case MetricBindingKind::NetworkDownload:
            return snapshot.network.downloadMbps;
        case MetricBindingKind::StorageRead:
            return snapshot.storage.readMbps;
        case MetricBindingKind::StorageWrite:
            return snapshot.storage.writeMbps;
        default:
            return 0.0;
    }
}

void InitializeThroughputSharedState(const SystemSnapshot& snapshot, MetricSource::ThroughputSharedState& state) {
    const MetricSource::ThroughputSharedState::HistoryEntry* networkHistories[2] = {};
    const MetricSource::ThroughputSharedState::HistoryEntry* storageHistories[2] = {};
    size_t networkHistoryCount = 0;
    size_t storageHistoryCount = 0;
    state.historyCount = 0;
    for (const auto& binding : kExactBindings) {
        if (!BindingSupportsPayload(binding, MetricPayloadKind::Throughput)) {
            continue;
        }
        if (state.historyCount >= std::size(state.histories)) {
            break;
        }
        auto& entry = state.histories[state.historyCount++];
        entry.metricRef = binding.key;
        ResolveRetainedThroughputHistory(snapshot,
            binding,
            ResolveThroughputValue(snapshot, binding.kind),
            entry.samples,
            entry.liveLeaderMbps,
            entry.plotShiftSamples);
        if (binding.throughputGroup == ThroughputGraphGroup::Network &&
            networkHistoryCount < std::size(networkHistories)) {
            networkHistories[networkHistoryCount++] = &entry;
        } else if (binding.throughputGroup == ThroughputGraphGroup::Storage &&
            storageHistoryCount < std::size(storageHistories)) {
            storageHistories[storageHistoryCount++] = &entry;
        }
    }
    state.networkMaxGraph = GetThroughputGraphMax(networkHistories, networkHistoryCount);
    state.storageMaxGraph = GetThroughputGraphMax(storageHistories, storageHistoryCount);
    state.timeMarkerOffsetSamples = GetTimeMarkerOffsetSamples(snapshot.now);
}

std::string TwoDigit(int value) {
    return FormatText("%02d", value);
}

std::string NumberText(int value) {
    return FormatText("%d", value);
}

std::string MonthName(int month) {
    static constexpr const char* kNames[]{
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December",
    };
    return month >= 1 && month <= 12 ? std::string(kNames[static_cast<size_t>(month - 1)]) : std::string{};
}

std::string MonthShortName(int month) {
    static constexpr const char* kNames[]{
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return month >= 1 && month <= 12 ? std::string(kNames[static_cast<size_t>(month - 1)]) : std::string{};
}

std::string WeekdayName(int dayOfWeek) {
    static constexpr const char* kNames[]{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return dayOfWeek >= 0 && dayOfWeek <= 6 ? std::string(kNames[static_cast<size_t>(dayOfWeek)]) : std::string{};
}

std::string WeekdayShortName(int dayOfWeek) {
    static constexpr const char* kNames[]{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return dayOfWeek >= 0 && dayOfWeek <= 6 ? std::string(kNames[static_cast<size_t>(dayOfWeek)]) : std::string{};
}

enum class FormatTokenKind : std::uint8_t {
    Hour24TwoDigit,
    Hour24,
    Hour12TwoDigit,
    Hour12,
    MinuteTwoDigit,
    Minute,
    SecondTwoDigit,
    Second,
    UpperMeridiem,
    LowerMeridiem,
    YearFull,
    YearShort,
    MonthName,
    MonthShortName,
    MonthTwoDigit,
    Month,
    DayTwoDigit,
    Day,
    WeekdayName,
    WeekdayShortName,
};

struct FormatToken {
    const char* text = "";
    std::uint8_t length = 0;
    FormatTokenKind kind = FormatTokenKind::Hour24TwoDigit;
};

static_assert(sizeof(FormatToken) == 16);

std::string ResolveFormatToken(const SYSTEMTIME& time, FormatTokenKind kind) {
    switch (kind) {
        case FormatTokenKind::Hour24TwoDigit:
            return TwoDigit(time.wHour);
        case FormatTokenKind::Hour24:
            return NumberText(time.wHour);
        case FormatTokenKind::Hour12TwoDigit: {
            const int hour = time.wHour % 12;
            return TwoDigit(hour == 0 ? 12 : hour);
        }
        case FormatTokenKind::Hour12: {
            const int hour = time.wHour % 12;
            return NumberText(hour == 0 ? 12 : hour);
        }
        case FormatTokenKind::MinuteTwoDigit:
            return TwoDigit(time.wMinute);
        case FormatTokenKind::Minute:
            return NumberText(time.wMinute);
        case FormatTokenKind::SecondTwoDigit:
            return TwoDigit(time.wSecond);
        case FormatTokenKind::Second:
            return NumberText(time.wSecond);
        case FormatTokenKind::UpperMeridiem:
            return time.wHour < 12 ? std::string("AM") : std::string("PM");
        case FormatTokenKind::LowerMeridiem:
            return time.wHour < 12 ? std::string("am") : std::string("pm");
        case FormatTokenKind::YearFull:
            return NumberText(time.wYear);
        case FormatTokenKind::YearShort:
            return TwoDigit(time.wYear % 100);
        case FormatTokenKind::MonthName:
            return MonthName(time.wMonth);
        case FormatTokenKind::MonthShortName:
            return MonthShortName(time.wMonth);
        case FormatTokenKind::MonthTwoDigit:
            return TwoDigit(time.wMonth);
        case FormatTokenKind::Month:
            return NumberText(time.wMonth);
        case FormatTokenKind::DayTwoDigit:
            return TwoDigit(time.wDay);
        case FormatTokenKind::Day:
            return NumberText(time.wDay);
        case FormatTokenKind::WeekdayName:
            return WeekdayName(time.wDayOfWeek);
        case FormatTokenKind::WeekdayShortName:
            return WeekdayShortName(time.wDayOfWeek);
    }
    return {};
}

std::string FormatWithTokens(
    const SYSTEMTIME& time, std::string_view format, const FormatToken* tokens, size_t tokenCount) {
    std::string output;
    for (size_t index = 0; index < format.size();) {
        bool matched = false;
        for (size_t tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex) {
            const FormatToken& token = tokens[tokenIndex];
            if (format.size() - index >= token.length &&
                format.compare(index, token.length, token.text, token.length) == 0) {
                AppendFormat(output, "%s", ResolveFormatToken(time, token.kind).c_str());
                index += token.length;
                matched = true;
                break;
            }
        }
        if (!matched) {
            output.push_back(format[index]);
            ++index;
        }
    }
    return output;
}

}  // namespace

std::string FormatClockTime(const SYSTEMTIME& time, std::string_view format) {
    static constexpr FormatToken kTokens[]{
        {"HH", 2, FormatTokenKind::Hour24TwoDigit},
        {"H", 1, FormatTokenKind::Hour24},
        {"hh", 2, FormatTokenKind::Hour12TwoDigit},
        {"h", 1, FormatTokenKind::Hour12},
        {"MM", 2, FormatTokenKind::MinuteTwoDigit},
        {"M", 1, FormatTokenKind::Minute},
        {"SS", 2, FormatTokenKind::SecondTwoDigit},
        {"S", 1, FormatTokenKind::Second},
        {"AM", 2, FormatTokenKind::UpperMeridiem},
        {"am", 2, FormatTokenKind::LowerMeridiem},
    };
    return FormatWithTokens(time, format, kTokens, sizeof(kTokens) / sizeof(kTokens[0]));
}

std::string FormatClockDate(const SYSTEMTIME& time, std::string_view format) {
    static constexpr FormatToken kTokens[]{
        {"YYYY", 4, FormatTokenKind::YearFull},
        {"YY", 2, FormatTokenKind::YearShort},
        {"MMMM", 4, FormatTokenKind::MonthName},
        {"MMM", 3, FormatTokenKind::MonthShortName},
        {"MM", 2, FormatTokenKind::MonthTwoDigit},
        {"M", 1, FormatTokenKind::Month},
        {"DD", 2, FormatTokenKind::DayTwoDigit},
        {"D", 1, FormatTokenKind::Day},
        {"dddd", 4, FormatTokenKind::WeekdayName},
        {"ddd", 3, FormatTokenKind::WeekdayShortName},
    };
    return FormatWithTokens(time, format, kTokens, sizeof(kTokens) / sizeof(kTokens[0]));
}

bool IsStaticTextMetric(std::string_view metricRef) {
    const MetricBindingMatch match = FindMetricBinding(metricRef);
    return match.binding != nullptr && BindingHasFlag(*match.binding, kStaticTextFlag) &&
        BindingSupportsPayload(*match.binding, MetricPayloadKind::Text);
}

std::optional<MetricDisplayStyle> FindMetricDisplayStyle(std::string_view metricRef) {
    const MetricBindingMatch match = FindMetricBinding(metricRef);
    return match.binding != nullptr && BindingHasFlag(*match.binding, kHasMetricStyleFlag) ?
        std::optional<MetricDisplayStyle>(match.binding->metricStyle) :
        std::nullopt;
}

ConfigMetricCatalog TelemetryMetricCatalog() {
    return ConfigMetricCatalog{&FindMetricDisplayStyle, &IsGenerallyAvailableMetric};
}

bool IsGenerallyAvailableMetric(std::string_view metricRef) {
    const MetricBindingMatch match = FindMetricBinding(metricRef);
    return match.binding != nullptr && BindingHasFlag(*match.binding, kGenerallyAvailableFlag);
}

std::string ResolveMetricSampleValueText(const MetricsSectionConfig& metrics, const std::string& metricRef) {
    const MetricDefinitionConfig* definition = FindEffectiveMetricDefinition(metrics, metricRef);
    if (definition == nullptr) {
        return {};
    }

    const MetricBindingMatch match = FindMetricBinding(metricRef);
    if (match.binding != nullptr && !BindingSupportsPayload(*match.binding, MetricPayloadKind::Value) &&
        !BindingSupportsPayload(*match.binding, MetricPayloadKind::Throughput)) {
        return {};
    }
    return BuildMetricSampleValueText(*definition, metricRef);
}

MetricSource::MetricSource(const SystemSnapshot& snapshot, const MetricsSectionConfig& metrics) :
    snapshot_(snapshot),
    metrics_(metrics) {}

const std::string& MetricSource::ResolveText(const std::string& metricRef) const {
    if (textCached_ && textCacheKey_ == metricRef) {
        return textCache_;
    }

    std::string resolved = "N/A";
    const MetricBindingMatch match = FindMetricBinding(metricRef);
    if (match.binding != nullptr && BindingSupportsPayload(*match.binding, MetricPayloadKind::Text)) {
        resolved = ResolveTextByKind(snapshot_, match.binding->kind);
    }
    textCacheKey_ = metricRef;
    textCache_ = std::move(resolved);
    textCached_ = true;
    return textCache_;
}

const MetricSource::MetricCacheEntry& MetricSource::CacheMetric(const std::string& metricRef) const {
    for (size_t i = 0; i < metricCacheCount_; ++i) {
        if (metricCache_[i].key == metricRef) {
            return metricCache_[i];
        }
    }

    const size_t slot = metricCacheCount_ < std::size(metricCache_) ? metricCacheCount_++ : std::size(metricCache_) - 1;
    auto& entry = metricCache_[slot];
    MetricValue metric;
    entry.resolved = ResolveMetricValue(snapshot_, metrics_, metricRef, metric);
    entry.key = metricRef;
    entry.metric = std::move(metric);
    return entry;
}

const MetricValue* MetricSource::FindMetric(const std::string& metricRef) const {
    const MetricCacheEntry& entry = CacheMetric(metricRef);
    return entry.resolved ? &entry.metric : nullptr;
}

const MetricValue& MetricSource::ResolveMetric(const std::string& metricRef) const {
    return CacheMetric(metricRef).metric;
}

const ThroughputMetric& MetricSource::ResolveThroughput(const std::string& metricRef) const {
    if (throughputCached_ && throughputCacheKey_ == metricRef) {
        return throughputCache_;
    }

    if (!throughputSharedStateReady_) {
        InitializeThroughputSharedState(snapshot_, throughputSharedState_);
        throughputSharedStateReady_ = true;
    }

    const MetricBindingMatch match = FindMetricBinding(metricRef);
    const MetricDefinitionConfig* definition = FindMetricDefinition(metrics_, metricRef);
    ThroughputMetric metric;
    if (match.binding != nullptr && BindingSupportsPayload(*match.binding, MetricPayloadKind::Throughput)) {
        const auto* history = FindThroughputHistory(throughputSharedState_, metricRef);
        const std::vector<double> emptyHistory;
        const std::vector<double>& resolvedHistory = history != nullptr ? history->samples : emptyHistory;
        metric.liveLeaderMbps =
            history != nullptr ? history->liveLeaderMbps : ResolveThroughputValue(snapshot_, match.binding->kind);
        metric.valueMbps = FiniteNonNegativeOr(metric.liveLeaderMbps);
        metric.history = resolvedHistory;
        metric.plotShiftSamples = history != nullptr ? history->plotShiftSamples : 0.0;
        metric.maxGraph = ResolveThroughputGraphMax(throughputSharedState_, *match.binding);
        metric.guideStepMbps = GetThroughputGuideStep(metric.maxGraph);
    }
    metric.timeMarkerOffsetSamples = throughputSharedState_.timeMarkerOffsetSamples;
    metric.timeMarkerIntervalSamples = kThroughputTimeMarkerIntervalSamples;
    if (definition != nullptr) {
        metric.label = definition->label;
        metric.valueText = FormatMetricValueText(*definition, metricRef, metric.valueMbps);
    }
    throughputCacheKey_ = metricRef;
    throughputCache_ = std::move(metric);
    throughputCached_ = true;
    return throughputCache_;
}

const std::string& MetricSource::ResolveNetworkFooter() const {
    if (!networkFooterCached_) {
        if (snapshot_.network.adapterName.empty()) {
            networkFooterCache_ = snapshot_.network.ipAddress;
        } else {
            networkFooterCache_ =
                FormatText("%s | %s", snapshot_.network.adapterName.c_str(), snapshot_.network.ipAddress.c_str());
        }
        networkFooterCached_ = true;
    }
    return networkFooterCache_;
}

void MetricSource::InitializeDriveRows() const {
    driveUsageDefinition_ = FindMetricDefinition(metrics_, "drive.usage");
    driveFreeDefinition_ = FindMetricDefinition(metrics_, "drive.free");
    driveRowsTotalReadMbps_ = 0.0;
    driveRowsTotalWriteMbps_ = 0.0;
    for (const auto& drive : snapshot_.drives) {
        driveRowsTotalReadMbps_ += FiniteNonNegativeOr(drive.readMbps);
        driveRowsTotalWriteMbps_ += FiniteNonNegativeOr(drive.writeMbps);
    }
    driveRowsCached_ = true;
}

const MetricSource::DriveRowCacheEntry& MetricSource::CacheDriveRow(size_t rowIndex) const {
    for (size_t i = 0; i < driveRowCacheCount_; ++i) {
        if (driveRowCache_[i].rowIndex == rowIndex) {
            return driveRowCache_[i];
        }
    }

    if (!driveRowsCached_) {
        InitializeDriveRows();
    }

    const size_t slot =
        driveRowCacheCount_ < std::size(driveRowCache_) ? driveRowCacheCount_++ : std::size(driveRowCache_) - 1;
    auto& entry = driveRowCache_[slot];
    const auto& drive = snapshot_.drives[rowIndex];
    const double readActivity = driveRowsTotalReadMbps_ > 0.0 ?
        ClampFinite(FiniteNonNegativeOr(drive.readMbps) / driveRowsTotalReadMbps_, 0.0, 1.0) :
        0.0;
    const double writeActivity = driveRowsTotalWriteMbps_ > 0.0 ?
        ClampFinite(FiniteNonNegativeOr(drive.writeMbps) / driveRowsTotalWriteMbps_, 0.0, 1.0) :
        0.0;
    entry.row = DriveRow{drive.label,
        readActivity,
        writeActivity,
        ClampFinite(drive.usedPercent, 0.0, 100.0),
        driveUsageDefinition_ != nullptr ?
            FormatMetricValueText(*driveUsageDefinition_, "drive.usage", drive.usedPercent) :
            std::string{},
        driveFreeDefinition_ != nullptr ? FormatMetricValueText(*driveFreeDefinition_, "drive.free", drive.freeGb) :
            std::string{}};
    entry.rowIndex = rowIndex;
    return entry;
}

const DriveRow* MetricSource::FindDriveRow(size_t rowIndex) const {
    if (rowIndex >= snapshot_.drives.size()) {
        return nullptr;
    }
    return &CacheDriveRow(rowIndex).row;
}

const std::string& MetricSource::ResolveClockTime(std::string_view format) const {
    if (clockTimeCached_ && clockTimeCacheKey_ == format) {
        return clockTimeCache_;
    }
    clockTimeCacheKey_ = format;
    clockTimeCache_ = FormatClockTime(snapshot_.now, format);
    clockTimeCached_ = true;
    return clockTimeCache_;
}

const std::string& MetricSource::ResolveClockDate(std::string_view format) const {
    if (clockDateCached_ && clockDateCacheKey_ == format) {
        return clockDateCache_;
    }
    clockDateCacheKey_ = format;
    clockDateCache_ = FormatClockDate(snapshot_.now, format);
    clockDateCached_ = true;
    return clockDateCache_;
}
