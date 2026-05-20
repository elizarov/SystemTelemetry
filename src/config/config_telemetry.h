#pragma once

#include <string>
#include <string_view>

#include "config/config.h"
#include "config/telemetry_settings.h"

const MetricDefinitionConfig* FindMetricDefinition(const MetricsSectionConfig& metrics, std::string_view id);
MetricDefinitionConfig* FindMetricDefinition(MetricsSectionConfig& metrics, std::string_view id);
bool IsRuntimePlaceholderMetricId(std::string_view id);
const MetricDefinitionConfig* FindEffectiveMetricDefinition(const MetricsSectionConfig& metrics, std::string_view id);
TelemetrySelectionSettings ExtractTelemetrySelectionSettings(const AppConfig& config);
TelemetrySettings ExtractTelemetrySettings(const AppConfig& config);
void ApplyResolvedTelemetrySelections(AppConfig& config, const ResolvedTelemetrySelections& resolvedSelections);
AppConfig BuildEffectiveRuntimeConfig(const AppConfig& uiConfig, const ResolvedTelemetrySelections& resolvedSelections);
std::string FormatMetricDefinitionValue(const MetricDefinitionConfig& definition);
