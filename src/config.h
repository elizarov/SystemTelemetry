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
    CONFIG_VALUE(UiFontConfig, title, "title", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontConfig, big, "big", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontConfig, value, "value", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontConfig, label, "label", configschema::FontSpecCodec);
    CONFIG_VALUE(UiFontConfig, smallText, "small", configschema::FontSpecCodec);
    CONFIG_SECTION("fonts");
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
    CONFIG_VALUE(std::string, monitorName, "monitor_name", configschema::StringCodec);
    CONFIG_VALUE(std::string, wallpaper, "wallpaper", configschema::StringCodec);
    CONFIG_VALUE(LogicalPointConfig, position, "position", configschema::LogicalPointCodec);
    CONFIG_SECTION("display");
};

struct NetworkConfig {
    CONFIG_REFLECTED_STRUCT(NetworkConfig)
    CONFIG_VALUE(std::string, adapterName, "adapter_name", configschema::StringCodec);
    CONFIG_SECTION("network");
};

struct DashboardSectionConfig {
    CONFIG_REFLECTED_STRUCT(DashboardSectionConfig)
    CONFIG_VALUE(int, outerMargin, "outer_margin", configschema::IntCodec);
    CONFIG_VALUE(int, rowGap, "row_gap", configschema::IntCodec);
    CONFIG_VALUE(int, cardGap, "card_gap", configschema::IntCodec);
    CONFIG_SECTION("dashboard");
};

struct CardStyleConfig {
    CONFIG_REFLECTED_STRUCT(CardStyleConfig)
    CONFIG_VALUE(int, cardPadding, "card_padding", configschema::IntCodec);
    CONFIG_VALUE(int, cardRadius, "card_radius", configschema::IntCodec);
    CONFIG_VALUE(int, cardBorderWidth, "card_border", configschema::IntCodec);
    CONFIG_VALUE(int, headerHeight, "header_height", configschema::IntCodec);
    CONFIG_VALUE(int, headerIconSize, "header_icon_size", configschema::IntCodec);
    CONFIG_VALUE(int, headerGap, "header_gap", configschema::IntCodec);
    CONFIG_VALUE(int, contentGap, "content_gap", configschema::IntCodec);
    CONFIG_VALUE(int, columnGap, "column_gap", configschema::IntCodec);
    CONFIG_VALUE(int, widgetLineGap, "widget_line_gap", configschema::IntCodec);
    CONFIG_SECTION("card_style");
};

struct ColorConfig {
    CONFIG_REFLECTED_STRUCT(ColorConfig)
    CONFIG_VALUE(unsigned int, backgroundColor, "background_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, foregroundColor, "foreground_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, accentColor, "accent_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, panelBorderColor, "panel_border_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, mutedTextColor, "muted_text_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, trackColor, "track_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, panelFillColor, "panel_fill_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, graphBackgroundColor, "graph_background_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, graphGridColor, "graph_grid_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, graphAxisColor, "graph_axis_color", configschema::HexColorCodec);
    CONFIG_VALUE(unsigned int, graphMarkerColor, "graph_marker_color", configschema::HexColorCodec);
    CONFIG_SECTION("colors");
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::string parameter;
    std::vector<LayoutNodeConfig> children;
};

struct LayoutSectionConfig {
    CONFIG_REFLECTED_STRUCT(LayoutSectionConfig)
    CONFIG_VALUE(LogicalSizeConfig, window, "window", configschema::LogicalSizeCodec);
    CONFIG_VALUE(LayoutNodeConfig, cardsLayout, "cards", configschema::LayoutExpressionCodec);
    CONFIG_SECTION("layout");
};

struct LayoutCardConfig {
    std::string id;

    CONFIG_REFLECTED_STRUCT(LayoutCardConfig)
    CONFIG_VALUE(std::string, title, "title", configschema::StringCodec);
    CONFIG_VALUE(std::string, icon, "icon", configschema::StringCodec);
    CONFIG_VALUE(LayoutNodeConfig, layout, "layout", configschema::LayoutExpressionCodec);
    CONFIG_DYNAMIC_SECTION("card.");
};

struct MetricScaleConfig {
    CONFIG_REFLECTED_STRUCT(MetricScaleConfig)
    CONFIG_VALUE(double, cpuClockGHz, "cpu_clock_ghz", configschema::DoubleCodec);
    CONFIG_VALUE(double, gpuTemperatureC, "gpu_temperature_c", configschema::DoubleCodec);
    CONFIG_VALUE(double, gpuClockMHz, "gpu_clock_mhz", configschema::DoubleCodec);
    CONFIG_VALUE(double, gpuFanRpm, "gpu_fan_rpm", configschema::DoubleCodec);
    CONFIG_VALUE(double, boardTemperatureC, "board_temperature_c", configschema::DoubleCodec);
    CONFIG_VALUE(double, boardFanRpm, "board_fan_rpm", configschema::DoubleCodec);
    CONFIG_SECTION("metric_scales");
};

struct MetricListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(MetricListWidgetConfig)
    CONFIG_VALUE(int, labelWidth, "label_width", configschema::IntCodec);
    CONFIG_VALUE(int, valueGap, "value_gap", configschema::IntCodec);
    CONFIG_VALUE(int, barHeight, "bar_height", configschema::IntCodec);
    CONFIG_VALUE(int, verticalGap, "vertical_gap", configschema::IntCodec);
    CONFIG_SECTION("metric_list");
};

struct DriveUsageListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(DriveUsageListWidgetConfig)
    CONFIG_VALUE(int, freeWidth, "free_width", configschema::IntCodec);
    CONFIG_VALUE(int, activityWidth, "activity_width", configschema::IntCodec);
    CONFIG_VALUE(int, barGap, "bar_gap", configschema::IntCodec);
    CONFIG_VALUE(int, valueGap, "value_gap", configschema::IntCodec);
    CONFIG_VALUE(int, percentGap, "percent_gap", configschema::IntCodec);
    CONFIG_VALUE(int, barHeight, "bar_height", configschema::IntCodec);
    CONFIG_VALUE(int, verticalGap, "vertical_gap", configschema::IntCodec);
    CONFIG_VALUE(int, labelPadding, "label_padding", configschema::IntCodec);
    CONFIG_VALUE(int, percentPadding, "percent_padding", configschema::IntCodec);
    CONFIG_VALUE(int, activitySegments, "activity_segments", configschema::IntCodec);
    CONFIG_VALUE(int, activitySegmentGap, "activity_segment_gap", configschema::IntCodec);
    CONFIG_SECTION("drive_usage_list");
};

struct ThroughputWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ThroughputWidgetConfig)
    CONFIG_VALUE(int, headerGap, "header_gap", configschema::IntCodec);
    CONFIG_VALUE(int, graphHeight, "graph_height", configschema::IntCodec);
    CONFIG_VALUE(int, valuePadding, "value_padding", configschema::IntCodec);
    CONFIG_VALUE(int, labelPadding, "label_padding", configschema::IntCodec);
    CONFIG_VALUE(int, axisPadding, "axis_padding", configschema::IntCodec);
    CONFIG_VALUE(int, scaleLabelPadding, "scale_label_padding", configschema::IntCodec);
    CONFIG_VALUE(int, scaleLabelMinHeight, "scale_label_min_height", configschema::IntCodec);
    CONFIG_VALUE(int, guideStrokeWidth, "guide_stroke_width", configschema::IntCodec);
    CONFIG_VALUE(int, plotStrokeWidth, "plot_stroke_width", configschema::IntCodec);
    CONFIG_VALUE(int, leaderDiameter, "leader_diameter", configschema::IntCodec);
    CONFIG_SECTION("throughput");
};

struct GaugeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(GaugeWidgetConfig)
    CONFIG_VALUE(int, preferredSize, "preferred_size", configschema::IntCodec);
    CONFIG_VALUE(int, outerPadding, "outer_padding", configschema::IntCodec);
    CONFIG_VALUE(int, minRadius, "min_radius", configschema::IntCodec);
    CONFIG_VALUE(int, ringThickness, "ring_thickness", configschema::IntCodec);
    CONFIG_VALUE(double, sweepDegrees, "sweep_degrees", configschema::DoubleCodec);
    CONFIG_VALUE(int, segmentCount, "segment_count", configschema::IntCodec);
    CONFIG_VALUE(double, segmentGapDegrees, "segment_gap_degrees", configschema::DoubleCodec);
    CONFIG_VALUE(int, textHalfWidth, "text_half_width", configschema::IntCodec);
    CONFIG_VALUE(int, valueTop, "value_top", configschema::IntCodec);
    CONFIG_VALUE(int, valueBottom, "value_bottom", configschema::IntCodec);
    CONFIG_VALUE(int, labelTop, "label_top", configschema::IntCodec);
    CONFIG_VALUE(int, labelBottom, "label_bottom", configschema::IntCodec);
    CONFIG_SECTION("gauge");
};

struct TextWidgetConfig {
    CONFIG_REFLECTED_STRUCT(TextWidgetConfig)
    CONFIG_VALUE(int, preferredPadding, "preferred_padding", configschema::IntCodec);
    CONFIG_SECTION("text");
};

struct NetworkFooterWidgetConfig {
    CONFIG_REFLECTED_STRUCT(NetworkFooterWidgetConfig)
    CONFIG_VALUE(int, preferredPadding, "preferred_padding", configschema::IntCodec);
    CONFIG_SECTION("network_footer");
};

struct ClockTimeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ClockTimeWidgetConfig)
    CONFIG_VALUE(int, padding, "padding", configschema::IntCodec);
    CONFIG_SECTION("clock_time");
};

struct ClockDateWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ClockDateWidgetConfig)
    CONFIG_VALUE(int, padding, "padding", configschema::IntCodec);
    CONFIG_SECTION("clock_date");
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
