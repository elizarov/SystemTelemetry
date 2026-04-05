#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct UiFontConfig {
    std::string face;
    int size = 0;
    int weight = 0;
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::string parameter;
    std::vector<LayoutNodeConfig> children;
};

struct LayoutCardConfig {
    std::string id;
    std::string title;
    std::string icon;
    LayoutNodeConfig layout;
};

struct MetricScaleConfig {
    double cpuClockGHz = 0.0;
    double gpuTemperatureC = 0.0;
    double gpuClockMHz = 0.0;
    double gpuFanRpm = 0.0;
    double boardTemperatureC = 0.0;
    double boardFanRpm = 0.0;
};

struct LayoutConfig {
    int windowWidth = 0;
    int windowHeight = 0;
    unsigned int backgroundColor = 0;
    unsigned int foregroundColor = 0;
    unsigned int accentColor = 0;
    unsigned int panelBorderColor = 0;
    unsigned int mutedTextColor = 0;
    unsigned int trackColor = 0;
    unsigned int panelFillColor = 0;
    unsigned int graphBackgroundColor = 0;
    unsigned int graphGridColor = 0;
    unsigned int graphAxisColor = 0;
    unsigned int graphMarkerColor = 0;

    int outerMargin = 0;
    int rowGap = 0;
    int cardGap = 0;
    int cardPadding = 0;
    int cardRadius = 0;
    int cardBorderWidth = 0;
    int headerHeight = 0;
    int headerIconSize = 0;
    int headerGap = 0;
    int contentGap = 0;
    int columnGap = 0;
    int metricLabelWidth = 0;
    int metricValueGap = 0;
    int metricBarHeight = 0;
    int metricVerticalGap = 0;
    int widgetLineGap = 0;
    int driveFreeWidth = 0;
    int driveBarGap = 0;
    int driveValueGap = 0;
    int driveBarHeight = 0;
    int driveVerticalGap = 0;
    int throughputHeaderGap = 0;
    int throughputGraphHeight = 0;
    int gaugePreferredSize = 0;
    int textPreferredPadding = 0;
    int footerPreferredPadding = 0;
    int clockTimePadding = 0;
    int clockDatePadding = 0;
    int throughputValuePadding = 0;
    int throughputLabelPadding = 0;
    int throughputAxisPadding = 0;
    int driveLabelPadding = 0;
    int drivePercentPadding = 0;
    int graphLabelPadding = 0;
    int graphLabelMinHeight = 0;
    int graphStrokeWidth = 0;
    int graphPlotStrokeWidth = 0;
    int gaugeOuterPadding = 0;
    int gaugeMinRadius = 0;
    int gaugeStrokeWidth = 0;
    int gaugeTextHalfWidth = 0;
    int gaugeValueTop = 0;
    int gaugeValueBottom = 0;
    int gaugeLabelTop = 0;
    int gaugeLabelBottom = 0;

    UiFontConfig titleFont{};
    UiFontConfig bigFont{};
    UiFontConfig valueFont{};
    UiFontConfig labelFont{};
    UiFontConfig smallFont{};

    LayoutNodeConfig cardsLayout;
    std::vector<LayoutCardConfig> cards;
};

struct AppConfig {
    std::string monitorName;
    int positionX = 0;
    int positionY = 0;
    std::string networkAdapter;
    std::vector<std::string> driveLetters;
    std::vector<std::string> boardTemperatureNames;
    std::vector<std::string> boardFanNames;
    MetricScaleConfig metricScales;
    LayoutConfig layout;
};

std::string LoadEmbeddedConfigTemplate();
AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
