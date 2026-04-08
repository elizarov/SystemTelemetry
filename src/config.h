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
CONFIG_CODEC(UiFontConfig, configschema::FontSpecCodec);

struct UiFontSetConfig {
    CONFIG_REFLECTED_STRUCT(UiFontSetConfig)
    CONFIG_VALUE(UiFontConfig, title, "title");
    CONFIG_VALUE(UiFontConfig, big, "big");
    CONFIG_VALUE(UiFontConfig, value, "value");
    CONFIG_VALUE(UiFontConfig, label, "label");
    CONFIG_VALUE(UiFontConfig, smallText, "small");
    CONFIG_SECTION("fonts");
};

struct LogicalPointConfig {
    int x = 0;
    int y = 0;
};
CONFIG_CODEC(LogicalPointConfig, configschema::LogicalPointCodec);

struct LogicalSizeConfig {
    int width = 0;
    int height = 0;
};
CONFIG_CODEC(LogicalSizeConfig, configschema::LogicalSizeCodec);

struct DisplayConfig {
    CONFIG_REFLECTED_STRUCT(DisplayConfig)
    CONFIG_VALUE(std::string, monitorName, "monitor_name");
    CONFIG_VALUE(std::string, layout, "layout");
    CONFIG_VALUE(std::string, wallpaper, "wallpaper");
    CONFIG_VALUE(LogicalPointConfig, position, "position");
    CONFIG_SECTION("display");
};

struct NetworkConfig {
    CONFIG_REFLECTED_STRUCT(NetworkConfig)
    CONFIG_VALUE(std::string, adapterName, "adapter_name");
    CONFIG_SECTION("network");
};

struct DashboardSectionConfig {
    CONFIG_REFLECTED_STRUCT(DashboardSectionConfig)
    CONFIG_VALUE(int, outerMargin, "outer_margin");
    CONFIG_VALUE(int, rowGap, "row_gap");
    CONFIG_VALUE(int, cardGap, "card_gap");
    CONFIG_SECTION("dashboard");
};

struct CardStyleConfig {
    CONFIG_REFLECTED_STRUCT(CardStyleConfig)
    CONFIG_VALUE(int, cardPadding, "card_padding");
    CONFIG_VALUE(int, cardRadius, "card_radius");
    CONFIG_VALUE(int, cardBorderWidth, "card_border");
    CONFIG_VALUE(int, headerHeight, "header_height");
    CONFIG_VALUE(int, headerIconSize, "header_icon_size");
    CONFIG_VALUE(int, headerGap, "header_gap");
    CONFIG_VALUE(int, contentGap, "content_gap");
    CONFIG_VALUE(int, columnGap, "column_gap");
    CONFIG_VALUE(int, widgetLineGap, "widget_line_gap");
    CONFIG_SECTION("card_style");
};

struct ColorConfig {
    CONFIG_REFLECTED_STRUCT(ColorConfig)
    CONFIG_VALUE(unsigned int, backgroundColor, "background_color");
    CONFIG_VALUE(unsigned int, foregroundColor, "foreground_color");
    CONFIG_VALUE(unsigned int, accentColor, "accent_color");
    CONFIG_VALUE(unsigned int, panelBorderColor, "panel_border_color");
    CONFIG_VALUE(unsigned int, mutedTextColor, "muted_text_color");
    CONFIG_VALUE(unsigned int, trackColor, "track_color");
    CONFIG_VALUE(unsigned int, panelFillColor, "panel_fill_color");
    CONFIG_VALUE(unsigned int, graphBackgroundColor, "graph_background_color");
    CONFIG_VALUE(unsigned int, graphGridColor, "graph_grid_color");
    CONFIG_VALUE(unsigned int, graphAxisColor, "graph_axis_color");
    CONFIG_VALUE(unsigned int, graphMarkerColor, "graph_marker_color");
    CONFIG_SECTION("colors");
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::string parameter;
    bool cardReference = false;
    std::vector<LayoutNodeConfig> children;
};
CONFIG_CODEC(LayoutNodeConfig, configschema::LayoutExpressionCodec);

struct LayoutSectionConfig {
    CONFIG_REFLECTED_STRUCT(LayoutSectionConfig)
    CONFIG_VALUE(LogicalSizeConfig, window, "window");
    CONFIG_VALUE(LayoutNodeConfig, cardsLayout, "cards");
    CONFIG_DYNAMIC_SECTION("layout.");
};

struct NamedLayoutSectionConfig {
    std::string name;

    CONFIG_REFLECTED_STRUCT(NamedLayoutSectionConfig)
    CONFIG_VALUE(LogicalSizeConfig, window, "window");
    CONFIG_VALUE(LayoutNodeConfig, cardsLayout, "cards");
    CONFIG_DYNAMIC_SECTION("layout.");
};

struct LayoutCardConfig {
    std::string id;

    CONFIG_REFLECTED_STRUCT(LayoutCardConfig)
    CONFIG_VALUE(std::string, title, "title");
    CONFIG_VALUE(std::string, icon, "icon");
    CONFIG_VALUE(LayoutNodeConfig, layout, "layout");
    CONFIG_DYNAMIC_SECTION("card.");
};

struct MetricScaleConfig {
    CONFIG_REFLECTED_STRUCT(MetricScaleConfig)
    CONFIG_VALUE(double, cpuClockGHz, "cpu_clock_ghz");
    CONFIG_VALUE(double, gpuTemperatureC, "gpu_temperature_c");
    CONFIG_VALUE(double, gpuClockMHz, "gpu_clock_mhz");
    CONFIG_VALUE(double, gpuFanRpm, "gpu_fan_rpm");
    CONFIG_VALUE(double, boardTemperatureC, "board_temperature_c");
    CONFIG_VALUE(double, boardFanRpm, "board_fan_rpm");
    CONFIG_SECTION("metric_scales");
};

struct MetricListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(MetricListWidgetConfig)
    CONFIG_VALUE(int, labelWidth, "label_width");
    CONFIG_VALUE(int, valueGap, "value_gap");
    CONFIG_VALUE(int, barHeight, "bar_height");
    CONFIG_VALUE(int, verticalGap, "vertical_gap");
    CONFIG_SECTION("metric_list");
};

struct DriveUsageListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(DriveUsageListWidgetConfig)
    CONFIG_VALUE(int, freeWidth, "free_width");
    CONFIG_VALUE(int, activityWidth, "activity_width");
    CONFIG_VALUE(int, barGap, "bar_gap");
    CONFIG_VALUE(int, valueGap, "value_gap");
    CONFIG_VALUE(int, percentGap, "percent_gap");
    CONFIG_VALUE(int, barHeight, "bar_height");
    CONFIG_VALUE(int, verticalGap, "vertical_gap");
    CONFIG_VALUE(int, labelPadding, "label_padding");
    CONFIG_VALUE(int, percentPadding, "percent_padding");
    CONFIG_VALUE(int, activitySegments, "activity_segments");
    CONFIG_VALUE(int, activitySegmentGap, "activity_segment_gap");
    CONFIG_SECTION("drive_usage_list");
};

struct ThroughputWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ThroughputWidgetConfig)
    CONFIG_VALUE(int, headerGap, "header_gap");
    CONFIG_VALUE(int, graphHeight, "graph_height");
    CONFIG_VALUE(int, valuePadding, "value_padding");
    CONFIG_VALUE(int, labelPadding, "label_padding");
    CONFIG_VALUE(int, axisPadding, "axis_padding");
    CONFIG_VALUE(int, scaleLabelPadding, "scale_label_padding");
    CONFIG_VALUE(int, scaleLabelMinHeight, "scale_label_min_height");
    CONFIG_VALUE(int, guideStrokeWidth, "guide_stroke_width");
    CONFIG_VALUE(int, plotStrokeWidth, "plot_stroke_width");
    CONFIG_VALUE(int, leaderDiameter, "leader_diameter");
    CONFIG_SECTION("throughput");
};

struct GaugeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(GaugeWidgetConfig)
    CONFIG_VALUE(int, preferredSize, "preferred_size");
    CONFIG_VALUE(int, outerPadding, "outer_padding");
    CONFIG_VALUE(int, minRadius, "min_radius");
    CONFIG_VALUE(int, ringThickness, "ring_thickness");
    CONFIG_VALUE(double, sweepDegrees, "sweep_degrees");
    CONFIG_VALUE(int, segmentCount, "segment_count");
    CONFIG_VALUE(double, segmentGapDegrees, "segment_gap_degrees");
    CONFIG_VALUE(int, textHalfWidth, "text_half_width");
    CONFIG_VALUE(int, valueTop, "value_top");
    CONFIG_VALUE(int, valueBottom, "value_bottom");
    CONFIG_VALUE(int, labelTop, "label_top");
    CONFIG_VALUE(int, labelBottom, "label_bottom");
    CONFIG_SECTION("gauge");
};

struct TextWidgetConfig {
    CONFIG_REFLECTED_STRUCT(TextWidgetConfig)
    CONFIG_VALUE(int, preferredPadding, "preferred_padding");
    CONFIG_SECTION("text");
};

struct NetworkFooterWidgetConfig {
    CONFIG_REFLECTED_STRUCT(NetworkFooterWidgetConfig)
    CONFIG_VALUE(int, preferredPadding, "preferred_padding");
    CONFIG_SECTION("network_footer");
};

struct ClockTimeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ClockTimeWidgetConfig)
    CONFIG_VALUE(int, padding, "padding");
    CONFIG_SECTION("clock_time");
};

struct ClockDateWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ClockDateWidgetConfig)
    CONFIG_VALUE(int, padding, "padding");
    CONFIG_SECTION("clock_date");
};

struct LayoutConfig {
    CONFIG_REFLECTED_BINDINGS(LayoutConfig)
    CONFIG_SECTION_VALUE(ColorConfig, colors);
    CONFIG_SECTION_VALUE(DashboardSectionConfig, dashboard);
    CONFIG_SECTION_VALUE(CardStyleConfig, cardStyle);
    CONFIG_SECTION_VALUE(MetricListWidgetConfig, metricList);
    CONFIG_SECTION_VALUE(DriveUsageListWidgetConfig, driveUsageList);
    CONFIG_SECTION_VALUE(ThroughputWidgetConfig, throughput);
    CONFIG_SECTION_VALUE(GaugeWidgetConfig, gauge);
    CONFIG_SECTION_VALUE(TextWidgetConfig, text);
    CONFIG_SECTION_VALUE(NetworkFooterWidgetConfig, networkFooter);
    CONFIG_SECTION_VALUE(ClockTimeWidgetConfig, clockTime);
    CONFIG_SECTION_VALUE(ClockDateWidgetConfig, clockDate);
    CONFIG_SECTION_VALUE(UiFontSetConfig, fonts);
    CONFIG_BINDING_LIST();

    LayoutSectionConfig structure{};
    LayoutNodeConfig cardsLayout;
    std::vector<LayoutCardConfig> cards;
};

struct AppConfig {
    CONFIG_REFLECTED_BINDINGS(AppConfig)
    CONFIG_SECTION_VALUE(DisplayConfig, display);
    CONFIG_SECTION_VALUE(NetworkConfig, network);
    CONFIG_SECTION_VALUE(MetricScaleConfig, metricScales);
    CONFIG_BINDING_LIST();

    std::vector<std::string> driveLetters;
    std::vector<std::string> boardTemperatureNames;
    std::vector<std::string> boardFanNames;
    std::unordered_map<std::string, std::string> boardTemperatureSensorNames;
    std::unordered_map<std::string, std::string> boardFanSensorNames;
    std::vector<NamedLayoutSectionConfig> layouts;
    LayoutConfig layout;
};

std::string LoadEmbeddedConfigTemplate();
AppConfig LoadConfig(const std::filesystem::path& path, bool includeOverlay = true);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
bool SaveFullConfig(const std::filesystem::path& path, const AppConfig& config);
bool SelectLayout(AppConfig& config, const std::string& name);
