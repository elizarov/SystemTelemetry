#include "config.h"

#include <sstream>

namespace {

constexpr std::string_view kRuntimePlaceholderMetricId = "nothing";

const MetricDefinitionConfig kRuntimePlaceholderMetricDefinition{
    std::string(kRuntimePlaceholderMetricId),
    MetricDisplayStyle::Scalar,
    false,
    1.0,
    "",
    "Nothing",
};

}  // namespace

ColorConfig ColorConfig::FromRgba(unsigned int value) {
    return ColorConfig{static_cast<std::uint32_t>(value)};
}

unsigned int ColorConfig::ToRgb() const {
    return (rgba >> 8) & 0xFFFFFFu;
}

unsigned int ColorConfig::ToRgba() const {
    return rgba;
}

std::uint8_t ColorConfig::Alpha() const {
    return static_cast<std::uint8_t>(rgba & 0xFFu);
}

const MetricDefinitionConfig* FindMetricDefinition(const MetricsSectionConfig& metrics, std::string_view id) {
    for (const auto& definition : metrics.definitions) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

MetricDefinitionConfig* FindMetricDefinition(MetricsSectionConfig& metrics, std::string_view id) {
    for (auto& definition : metrics.definitions) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

bool IsRuntimePlaceholderMetricId(std::string_view id) {
    return id == kRuntimePlaceholderMetricId;
}

const MetricDefinitionConfig* FindEffectiveMetricDefinition(const MetricsSectionConfig& metrics, std::string_view id) {
    const MetricDefinitionConfig* definition = FindMetricDefinition(metrics, id);
    if (definition != nullptr) {
        return definition;
    }
    return IsRuntimePlaceholderMetricId(id) ? &kRuntimePlaceholderMetricDefinition : nullptr;
}

TelemetrySelectionSettings ExtractTelemetrySelectionSettings(const AppConfig& config) {
    TelemetrySelectionSettings settings;
    settings.preferredAdapterName = config.network.adapterName;
    settings.configuredDrives = config.storage.drives;
    return settings;
}

TelemetrySettings ExtractTelemetrySettings(const AppConfig& config) {
    TelemetrySettings settings;
    settings.board.requestedTemperatureNames = config.layout.board.requestedTemperatureNames;
    settings.board.requestedFanNames = config.layout.board.requestedFanNames;
    settings.board.temperatureSensorNames = config.layout.board.temperatureSensorNames;
    settings.board.fanSensorNames = config.layout.board.fanSensorNames;
    settings.selection = ExtractTelemetrySelectionSettings(config);
    return settings;
}

AppConfig BuildEffectiveRuntimeConfig(
    const AppConfig& uiConfig, const ResolvedTelemetrySelections& resolvedSelections) {
    AppConfig config = uiConfig;
    if (!resolvedSelections.adapterName.empty()) {
        config.network.adapterName = resolvedSelections.adapterName;
    }
    config.storage.drives = resolvedSelections.drives;
    return config;
}

bool LayoutConfig::operator==(const LayoutConfig& other) const {
    return colors == other.colors && dashboard == other.dashboard && cardStyle == other.cardStyle &&
           metricList == other.metricList && driveUsageList == other.driveUsageList && throughput == other.throughput &&
           gauge == other.gauge && text == other.text && networkFooter == other.networkFooter &&
           layoutEditor == other.layoutEditor && fonts == other.fonts && board == other.board &&
           metrics == other.metrics && layouts == other.layouts && cards == other.cards &&
           structure == other.structure && cardsLayout == other.cardsLayout;
}

bool AppConfig::operator==(const AppConfig& other) const {
    return display == other.display && network == other.network && storage == other.storage && layout == other.layout;
}

std::string FormatMetricDefinitionValue(const MetricDefinitionConfig& definition) {
    std::ostringstream stream;
    if (definition.telemetryScale) {
        stream << "*";
    } else {
        stream << definition.scale;
    }
    stream << "," << definition.unit << "," << definition.label;
    return stream.str();
}
