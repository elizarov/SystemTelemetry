#include "config.h"

#include "dashboard_metrics.h"

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
           metricList == other.metricList && driveUsageList == other.driveUsageList &&
           throughput == other.throughput && gauge == other.gauge && text == other.text &&
           networkFooter == other.networkFooter && layoutEditor == other.layoutEditor && fonts == other.fonts &&
           board == other.board && metrics == other.metrics && layouts == other.layouts && cards == other.cards &&
           structure == other.structure && cardsLayout == other.cardsLayout;
}

bool AppConfig::operator==(const AppConfig& other) const {
    return display == other.display && network == other.network && storage == other.storage &&
           layout == other.layout;
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
