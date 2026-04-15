#include "config.h"

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
