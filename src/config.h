#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct UiFontConfig {
    std::string face;
    int size = 0;
    int weight = 0;
};

struct UiFontSetConfig {
    UiFontConfig title{};
    UiFontConfig big{};
    UiFontConfig value{};
    UiFontConfig label{};
    UiFontConfig smallText{};
};

struct LogicalPointConfig {
    int x = 0;
    int y = 0;
};

struct LogicalSizeConfig {
    int width = 0;
    int height = 0;
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

struct MetricListWidgetConfig {
    int labelWidth = 0;
    int valueGap = 0;
    int barHeight = 0;
    int verticalGap = 0;
};

struct DriveUsageListWidgetConfig {
    int freeWidth = 0;
    int activityWidth = 0;
    int barGap = 0;
    int valueGap = 0;
    int percentGap = 0;
    int barHeight = 0;
    int verticalGap = 0;
    int labelPadding = 0;
    int percentPadding = 0;
    int activitySegments = 0;
    int activitySegmentGap = 0;
};

struct ThroughputWidgetConfig {
    int headerGap = 0;
    int graphHeight = 0;
    int valuePadding = 0;
    int labelPadding = 0;
    int axisPadding = 0;
    int scaleLabelPadding = 0;
    int scaleLabelMinHeight = 0;
    int guideStrokeWidth = 0;
    int plotStrokeWidth = 0;
    int leaderDiameter = 0;
};

struct GaugeWidgetConfig {
    int preferredSize = 0;
    int outerPadding = 0;
    int minRadius = 0;
    int ringThickness = 0;
    double sweepDegrees = 0.0;
    int segmentCount = 0;
    double segmentGapDegrees = 0.0;
    int textHalfWidth = 0;
    int valueTop = 0;
    int valueBottom = 0;
    int labelTop = 0;
    int labelBottom = 0;
};

struct TextWidgetConfig {
    int preferredPadding = 0;
};

struct NetworkFooterWidgetConfig {
    int preferredPadding = 0;
};

struct ClockTimeWidgetConfig {
    int padding = 0;
};

struct ClockDateWidgetConfig {
    int padding = 0;
};

struct LayoutConfig {
    LogicalSizeConfig window{};
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
    int widgetLineGap = 0;

    MetricListWidgetConfig metricList{};
    DriveUsageListWidgetConfig driveUsageList{};
    ThroughputWidgetConfig throughput{};
    GaugeWidgetConfig gauge{};
    TextWidgetConfig text{};
    NetworkFooterWidgetConfig networkFooter{};
    ClockTimeWidgetConfig clockTime{};
    ClockDateWidgetConfig clockDate{};

    UiFontSetConfig fonts{};

    LayoutNodeConfig cardsLayout;
    std::vector<LayoutCardConfig> cards;
};

struct AppConfig {
    std::string monitorName;
    std::string wallpaper;
    LogicalPointConfig position{};
    std::string networkAdapter;
    std::vector<std::string> driveLetters;
    std::vector<std::string> boardTemperatureNames;
    std::vector<std::string> boardFanNames;
    std::unordered_map<std::string, std::string> boardTemperatureSensorNames;
    std::unordered_map<std::string, std::string> boardFanSensorNames;
    MetricScaleConfig metricScales;
    LayoutConfig layout;
};

std::string LoadEmbeddedConfigTemplate();
AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
