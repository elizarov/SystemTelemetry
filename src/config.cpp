#include "config.h"

#include <sstream>

namespace {

struct MetricDisplayStyleEntry {
    MetricDisplayStyle style;
    std::string_view name;
};

constexpr MetricDisplayStyleEntry kMetricDisplayStyleTable[] = {
    {MetricDisplayStyle::Scalar, "scalar"},
    {MetricDisplayStyle::Percent, "percent"},
    {MetricDisplayStyle::Memory, "memory"},
    {MetricDisplayStyle::Throughput, "throughput"},
    {MetricDisplayStyle::SizeAuto, "size_auto"},
    {MetricDisplayStyle::LabelOnly, "label_only"},
};

}  // namespace

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

AppConfig BuildEffectiveRuntimeConfig(const AppConfig& uiConfig, const AppConfig& resolvedRuntimeConfig) {
    AppConfig config = uiConfig;
    if (!resolvedRuntimeConfig.network.adapterName.empty()) {
        config.network.adapterName = resolvedRuntimeConfig.network.adapterName;
    }
    config.storage.drives = resolvedRuntimeConfig.storage.drives;
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
    for (const auto& entry : kMetricDisplayStyleTable) {
        if (entry.style == style) {
            return entry.name;
        }
    }
    return {};
}

bool ParseMetricDisplayStyle(std::string_view text, MetricDisplayStyle& style) {
    for (const auto& entry : kMetricDisplayStyleTable) {
        if (entry.name == text) {
            style = entry.style;
            return true;
        }
    }
    return false;
}
