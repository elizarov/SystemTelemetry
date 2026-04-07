#pragma once

#include "config_schema.h"

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
    CONFIG_REFLECTED_STRUCT(UiFontSetConfig)
    CONFIG_VALUE(UiFontSetConfig, UiFontConfig, title, "title", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontSetConfig, UiFontConfig, big, "big", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontSetConfig, UiFontConfig, value, "value", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontSetConfig, UiFontConfig, label, "label", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontSetConfig, UiFontConfig, smallText, "small", configschema::FontSpecCodec);
    CONFIG_AUTO_SECTION(UiFontSetConfig, "fonts");
};

struct LogicalPointConfig {
    int x = 0;
    int y = 0;
};

struct LogicalSizeConfig {
    int width = 0;
    int height = 0;
};

struct DisplayConfig {
    CONFIG_REFLECTED_STRUCT(DisplayConfig)
    CONFIG_VALUE(DisplayConfig, std::string, monitorName, "monitor_name", configschema::StringCodec);
    CONFIG_VALUE(DisplayConfig, std::string, wallpaper, "wallpaper", configschema::StringCodec);
    CONFIG_VALUE(DisplayConfig, LogicalPointConfig, position, "position", configschema::LogicalPointCodec);
    CONFIG_AUTO_SECTION(DisplayConfig, "display");
};

struct NetworkConfig {
    CONFIG_REFLECTED_STRUCT(NetworkConfig)
    CONFIG_VALUE(NetworkConfig, std::string, adapterName, "adapter_name", configschema::StringCodec);
    CONFIG_AUTO_SECTION(NetworkConfig, "network");
};

struct DashboardSectionConfig {
    CONFIG_REFLECTED_STRUCT(DashboardSectionConfig)
    CONFIG_VALUE(DashboardSectionConfig, int, outerMargin, "outer_margin", configschema::IntCodec);
    CONFIG_VALUE(DashboardSectionConfig, int, rowGap, "row_gap", configschema::IntCodec);
    CONFIG_VALUE(DashboardSectionConfig, int, cardGap, "card_gap", configschema::IntCodec);
    CONFIG_AUTO_SECTION(DashboardSectionConfig, "dashboard");
};

struct CardStyleConfig {
    CONFIG_REFLECTED_STRUCT(CardStyleConfig)
    CONFIG_VALUE(CardStyleConfig, int, cardPadding, "card_padding", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, cardRadius, "card_radius", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, cardBorderWidth, "card_border", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, headerHeight, "header_height", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, headerIconSize, "header_icon_size", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, headerGap, "header_gap", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, contentGap, "content_gap", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, columnGap, "column_gap", configschema::IntCodec);
    CONFIG_VALUE(CardStyleConfig, int, widgetLineGap, "widget_line_gap", configschema::IntCodec);
    CONFIG_AUTO_SECTION(CardStyleConfig, "card_style");
};

struct ColorConfig {
    CONFIG_REFLECTED_STRUCT(ColorConfig)
    CONFIG_VALUE(ColorConfig, unsigned int, backgroundColor, "background_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, foregroundColor, "foreground_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, accentColor, "accent_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, panelBorderColor, "panel_border_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, mutedTextColor, "muted_text_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, trackColor, "track_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, panelFillColor, "panel_fill_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, graphBackgroundColor, "graph_background_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, graphGridColor, "graph_grid_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, graphAxisColor, "graph_axis_color", configschema::HexColorCodec);
    CONFIG_VALUE(ColorConfig, unsigned int, graphMarkerColor, "graph_marker_color", configschema::HexColorCodec);
    CONFIG_AUTO_SECTION(ColorConfig, "colors");
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::string parameter;
    std::vector<LayoutNodeConfig> children;
};

struct LayoutSectionConfig {
    CONFIG_REFLECTED_STRUCT(LayoutSectionConfig)
    CONFIG_VALUE(LayoutSectionConfig, LogicalSizeConfig, window, "window", configschema::LogicalSizeCodec);
    CONFIG_VALUE(LayoutSectionConfig, LayoutNodeConfig, cardsLayout, "cards", configschema::LayoutExpressionCodec);
    CONFIG_AUTO_SECTION(LayoutSectionConfig, "layout");
};

struct LayoutCardConfig {
    std::string id;

    CONFIG_REFLECTED_STRUCT(LayoutCardConfig)
    CONFIG_VALUE(LayoutCardConfig, std::string, title, "title", configschema::StringCodec);
    CONFIG_VALUE(LayoutCardConfig, std::string, icon, "icon", configschema::StringCodec);
    CONFIG_VALUE(LayoutCardConfig, LayoutNodeConfig, layout, "layout", configschema::LayoutExpressionCodec);
    CONFIG_AUTO_DYNAMIC_SECTION(LayoutCardConfig, "card.");
};

struct MetricScaleConfig {
    CONFIG_REFLECTED_STRUCT(MetricScaleConfig)
    CONFIG_VALUE(MetricScaleConfig, double, cpuClockGHz, "cpu_clock_ghz", configschema::DoubleCodec);
    CONFIG_VALUE(MetricScaleConfig, double, gpuTemperatureC, "gpu_temperature_c", configschema::DoubleCodec);
    CONFIG_VALUE(MetricScaleConfig, double, gpuClockMHz, "gpu_clock_mhz", configschema::DoubleCodec);
    CONFIG_VALUE(MetricScaleConfig, double, gpuFanRpm, "gpu_fan_rpm", configschema::DoubleCodec);
    CONFIG_VALUE(MetricScaleConfig, double, boardTemperatureC, "board_temperature_c", configschema::DoubleCodec);
    CONFIG_VALUE(MetricScaleConfig, double, boardFanRpm, "board_fan_rpm", configschema::DoubleCodec);
    CONFIG_AUTO_SECTION(MetricScaleConfig, "metric_scales");
};

struct MetricListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(MetricListWidgetConfig)
    CONFIG_VALUE(MetricListWidgetConfig, int, labelWidth, "label_width", configschema::IntCodec);
    CONFIG_VALUE(MetricListWidgetConfig, int, valueGap, "value_gap", configschema::IntCodec);
    CONFIG_VALUE(MetricListWidgetConfig, int, barHeight, "bar_height", configschema::IntCodec);
    CONFIG_VALUE(MetricListWidgetConfig, int, verticalGap, "vertical_gap", configschema::IntCodec);
    CONFIG_AUTO_SECTION(MetricListWidgetConfig, "metric_list");
};

struct DriveUsageListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(DriveUsageListWidgetConfig)
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, freeWidth, "free_width", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, activityWidth, "activity_width", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, barGap, "bar_gap", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, valueGap, "value_gap", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, percentGap, "percent_gap", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, barHeight, "bar_height", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, verticalGap, "vertical_gap", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, labelPadding, "label_padding", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, percentPadding, "percent_padding", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, activitySegments, "activity_segments", configschema::IntCodec);
    CONFIG_VALUE(DriveUsageListWidgetConfig, int, activitySegmentGap, "activity_segment_gap", configschema::IntCodec);
    CONFIG_AUTO_SECTION(DriveUsageListWidgetConfig, "drive_usage_list");
};

struct ThroughputWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ThroughputWidgetConfig)
    CONFIG_VALUE(ThroughputWidgetConfig, int, headerGap, "header_gap", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, graphHeight, "graph_height", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, valuePadding, "value_padding", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, labelPadding, "label_padding", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, axisPadding, "axis_padding", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, scaleLabelPadding, "scale_label_padding", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, scaleLabelMinHeight, "scale_label_min_height", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, guideStrokeWidth, "guide_stroke_width", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, plotStrokeWidth, "plot_stroke_width", configschema::IntCodec);
    CONFIG_VALUE(ThroughputWidgetConfig, int, leaderDiameter, "leader_diameter", configschema::IntCodec);
    CONFIG_AUTO_SECTION(ThroughputWidgetConfig, "throughput");
};

struct GaugeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(GaugeWidgetConfig)
    CONFIG_VALUE(GaugeWidgetConfig, int, preferredSize, "preferred_size", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, outerPadding, "outer_padding", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, minRadius, "min_radius", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, ringThickness, "ring_thickness", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, double, sweepDegrees, "sweep_degrees", configschema::DoubleCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, segmentCount, "segment_count", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, double, segmentGapDegrees, "segment_gap_degrees", configschema::DoubleCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, textHalfWidth, "text_half_width", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, valueTop, "value_top", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, valueBottom, "value_bottom", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, labelTop, "label_top", configschema::IntCodec);
    CONFIG_VALUE(GaugeWidgetConfig, int, labelBottom, "label_bottom", configschema::IntCodec);
    CONFIG_AUTO_SECTION(GaugeWidgetConfig, "gauge");
};

struct TextWidgetConfig {
    CONFIG_REFLECTED_STRUCT(TextWidgetConfig)
    CONFIG_VALUE(TextWidgetConfig, int, preferredPadding, "preferred_padding", configschema::IntCodec);
    CONFIG_AUTO_SECTION(TextWidgetConfig, "text");
};

struct NetworkFooterWidgetConfig {
    CONFIG_REFLECTED_STRUCT(NetworkFooterWidgetConfig)
    CONFIG_VALUE(NetworkFooterWidgetConfig, int, preferredPadding, "preferred_padding", configschema::IntCodec);
    CONFIG_AUTO_SECTION(NetworkFooterWidgetConfig, "network_footer");
};

struct ClockTimeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ClockTimeWidgetConfig)
    CONFIG_VALUE(ClockTimeWidgetConfig, int, padding, "padding", configschema::IntCodec);
    CONFIG_AUTO_SECTION(ClockTimeWidgetConfig, "clock_time");
};

struct ClockDateWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ClockDateWidgetConfig)
    CONFIG_VALUE(ClockDateWidgetConfig, int, padding, "padding", configschema::IntCodec);
    CONFIG_AUTO_SECTION(ClockDateWidgetConfig, "clock_date");
};

struct LayoutConfig {
    LayoutSectionConfig structure{};
    ColorConfig colors{};
    DashboardSectionConfig dashboard{};
    CardStyleConfig cardStyle{};
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
    DisplayConfig display{};
    NetworkConfig network{};
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
