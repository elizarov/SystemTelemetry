#include "config.h"

#include <sstream>

ColorConfig ColorConfig::FromRgb(unsigned int value) {
    return ColorConfig{static_cast<std::uint32_t>(value & 0xFFFFFFu)};
}

unsigned int ColorConfig::ToRgb() const {
    return rgb & 0xFFFFFFu;
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

TelemetrySelectionSettings ExtractTelemetrySelectionSettings(const AppConfig& config) {
    TelemetrySelectionSettings settings;
    settings.preferredAdapterName = config.network.adapterName;
    settings.configuredDrives = config.storage.drives;
    return settings;
}

TelemetrySettings ExtractTelemetrySettings(const AppConfig& config) {
    TelemetrySettings settings;
    settings.board.requestedTemperatureNames = config.board.requestedTemperatureNames;
    settings.board.requestedFanNames = config.board.requestedFanNames;
    settings.board.temperatureSensorNames = config.board.temperatureSensorNames;
    settings.board.fanSensorNames = config.board.fanSensorNames;
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

std::string FormatMetricDefinitionValue(const MetricDefinitionConfig& definition) {
    std::ostringstream stream;
    stream << MetricDisplayStyleName(definition.style) << ",";
    if (definition.telemetryScale) {
        stream << "*";
    } else {
        stream << definition.scale;
    }
    stream << "," << definition.unit << "," << definition.label;
    return stream.str();
}

std::string_view MetricDisplayStyleName(MetricDisplayStyle style) {
    return EnumToString(style);
}

bool ParseMetricDisplayStyle(std::string_view text, MetricDisplayStyle& style) {
    return TryEnumFromString(text, style);
}
