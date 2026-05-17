#include "config/config.h"

#include "config/color_format.h"
#include "util/numeric_format.h"
#include "util/text_format.h"

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
    return ColorConfig{static_cast<std::uint32_t>(value), FormatRgbaColorText(value)};
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
    settings.preferredGpuAdapterName = config.gpu.adapterName;
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

void ApplyResolvedTelemetrySelections(AppConfig& config, const ResolvedTelemetrySelections& resolvedSelections) {
    if (!resolvedSelections.adapterName.empty()) {
        config.network.adapterName = resolvedSelections.adapterName;
    }
    if (!config.gpu.adapterName.empty() && !resolvedSelections.gpuAdapterName.empty()) {
        config.gpu.adapterName = resolvedSelections.gpuAdapterName;
    }
    config.storage.drives = resolvedSelections.drives;
    for (const auto& [logicalName, sensorName] : resolvedSelections.boardTemperatureSensorNames) {
        if (!sensorName.empty()) {
            config.layout.board.temperatureSensorNames[logicalName] = sensorName;
        }
    }
    for (const auto& [logicalName, sensorName] : resolvedSelections.boardFanSensorNames) {
        if (!sensorName.empty()) {
            config.layout.board.fanSensorNames[logicalName] = sensorName;
        }
    }
}

AppConfig BuildEffectiveRuntimeConfig(
    const AppConfig& uiConfig, const ResolvedTelemetrySelections& resolvedSelections) {
    AppConfig config = uiConfig;
    ApplyResolvedTelemetrySelections(config, resolvedSelections);
    return config;
}

std::string FormatMetricDefinitionValue(const MetricDefinitionConfig& definition) {
    return FormatText("%s,%s,%s",
        definition.telemetryScale ? "*" : FormatDoubleGeneral(definition.scale).c_str(),
        definition.unit.c_str(),
        definition.label.c_str());
}
