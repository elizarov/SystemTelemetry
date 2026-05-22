#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/metric_display_style.h"

struct ColorConfig {
    std::uint32_t rgba = 0x000000FFu;
    std::string expression;

    static ColorConfig FromRgba(unsigned int value);

    unsigned int ToRgb() const;
    unsigned int ToRgba() const;
    std::uint8_t Alpha() const;

    bool operator==(const ColorConfig& other) const = default;
};

inline bool operator==(const ColorConfig& color, unsigned int value) {
    return color.ToRgba() == value;
}

inline bool operator==(unsigned int value, const ColorConfig& color) {
    return color == value;
}

struct UiFontConfig {
    std::string face;
    int size = 0;
    int weight = 0;

    bool operator==(const UiFontConfig& other) const = default;
};

struct LogicalPointConfig {
    int x = 0;
    int y = 0;

    bool operator==(const LogicalPointConfig& other) const = default;
};

struct LogicalSizeConfig {
    int width = 0;
    int height = 0;

    bool operator==(const LogicalSizeConfig& other) const = default;
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::string parameter;
    bool cardReference = false;
    std::vector<LayoutNodeConfig> children;

    bool operator==(const LayoutNodeConfig& other) const = default;
};

struct MetricDefinitionConfig {
    std::string id;
    MetricDisplayStyle style = MetricDisplayStyle::Scalar;
    bool telemetryScale = false;
    double scale = 0.0;
    std::string unit;
    std::string label;

    bool operator==(const MetricDefinitionConfig& other) const = default;
};

// config_meta: custom_section [board] codec=BoardSectionCodec
struct BoardConfig {
    std::vector<std::string> requestedTemperatureNames;
    std::vector<std::string> requestedFanNames;
    std::unordered_map<std::string, std::string> temperatureSensorNames;
    std::unordered_map<std::string, std::string> fanSensorNames;

    bool operator==(const BoardConfig& other) const = default;
};

// config_meta: custom_section [metrics] codec=MetricsSectionCodec
struct MetricsSectionConfig {
    std::vector<MetricDefinitionConfig> definitions;

    bool operator==(const MetricsSectionConfig& other) const = default;
};
