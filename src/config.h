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
CONFIG_CODEC(std::vector<std::string>, configschema::StringCodec);

template <> struct configschema::DefaultLayoutEditTraits<UiFontConfig> {
    using type = typename configschema::LayoutEditTraitsForPolicy<configschema::FontSizePolicy>::type;
};

struct LayoutConfig;
struct AppConfig;

struct UiFontSetConfig {
    CONFIG_REFLECTED_STRUCT(UiFontSetConfig)
    CONFIG_EDITABLE_VALUE(UiFontConfig, title, "title");
    CONFIG_EDITABLE_VALUE(UiFontConfig, big, "big");
    CONFIG_EDITABLE_VALUE(UiFontConfig, value, "value");
    CONFIG_EDITABLE_VALUE(UiFontConfig, label, "label");
    CONFIG_EDITABLE_VALUE(UiFontConfig, text, "text");
    CONFIG_EDITABLE_VALUE(UiFontConfig, smallText, "small");
    CONFIG_EDITABLE_VALUE(UiFontConfig, footer, "footer");
    CONFIG_EDITABLE_VALUE(UiFontConfig, clockTime, "clock_time");
    CONFIG_EDITABLE_VALUE(UiFontConfig, clockDate, "clock_date");
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
    CONFIG_VALUE(double, scale, "scale");
    CONFIG_SECTION("display");
};

struct NetworkConfig {
    CONFIG_REFLECTED_STRUCT(NetworkConfig)
    CONFIG_VALUE(std::string, adapterName, "adapter_name");
    CONFIG_SECTION("network");
};

struct StorageConfig {
    CONFIG_REFLECTED_STRUCT(StorageConfig)
    CONFIG_VALUE(std::vector<std::string>, drives, "drives");
    CONFIG_SECTION("storage");
};

struct BoardConfig {
    CONFIG_REFLECTED_STRUCT(BoardConfig)
    CONFIG_SECTION("board");

    std::vector<std::string> requestedTemperatureNames;
    std::vector<std::string> requestedFanNames;
    std::unordered_map<std::string, std::string> temperatureSensorNames;
    std::unordered_map<std::string, std::string> fanSensorNames;
};

CONFIG_SECTION_CODEC(BoardConfig, configschema::BoardSectionCodec);

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
    CONFIG_VALUE(unsigned int, layoutGuideColor, "layout_guide_color");
    CONFIG_VALUE(unsigned int, activeEditColor, "active_edit_color");
    CONFIG_VALUE(unsigned int, panelBorderColor, "panel_border_color");
    CONFIG_VALUE(unsigned int, mutedTextColor, "muted_text_color");
    CONFIG_VALUE(unsigned int, trackColor, "track_color");
    CONFIG_VALUE(unsigned int, panelFillColor, "panel_fill_color");
    CONFIG_VALUE(unsigned int, graphBackgroundColor, "graph_background_color");
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
    std::string name;

    CONFIG_REFLECTED_STRUCT(LayoutSectionConfig)
    CONFIG_VALUE(std::string, description, "description");
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
    CONFIG_EDITABLE_VALUE(int, labelWidth, "label_width");
    CONFIG_EDITABLE_VALUE(int, barHeight, "bar_height");
    CONFIG_EDITABLE_VALUE(int, verticalGap, "vertical_gap");
    CONFIG_SECTION("metric_list");
};

struct DriveUsageListWidgetConfig {
    CONFIG_REFLECTED_STRUCT(DriveUsageListWidgetConfig)
    CONFIG_EDITABLE_VALUE(int, labelGap, "label_gap");
    CONFIG_EDITABLE_VALUE(int, activityWidth, "activity_width");
    CONFIG_EDITABLE_VALUE(int, rwGap, "rw_gap");
    CONFIG_EDITABLE_VALUE(int, barGap, "bar_gap");
    CONFIG_EDITABLE_VALUE(int, percentGap, "percent_gap");
    CONFIG_EDITABLE_VALUE(int, freeWidth, "free_width");
    CONFIG_EDITABLE_VALUE(int, barHeight, "bar_height");
    CONFIG_EDITABLE_VALUE(int, headerGap, "header_gap");
    CONFIG_EDITABLE_VALUE(int, rowGap, "row_gap");
    CONFIG_EDITABLE_VALUE(int, activitySegments, "activity_segments");
    CONFIG_EDITABLE_VALUE_WITH(int, activitySegmentGap, "activity_segment_gap", configschema::DriveUsageActivitySegmentGapPolicy);
    CONFIG_SECTION("drive_usage_list");
};

struct ThroughputWidgetConfig {
    CONFIG_REFLECTED_STRUCT(ThroughputWidgetConfig)
    CONFIG_EDITABLE_VALUE(int, headerGap, "header_gap");
    CONFIG_EDITABLE_VALUE(int, axisPadding, "axis_padding");
    CONFIG_EDITABLE_VALUE(int, guideStrokeWidth, "guide_stroke_width");
    CONFIG_EDITABLE_VALUE(int, plotStrokeWidth, "plot_stroke_width");
    CONFIG_EDITABLE_VALUE(int, leaderDiameter, "leader_diameter");
    CONFIG_SECTION("throughput");
};

struct GaugeWidgetConfig {
    CONFIG_REFLECTED_STRUCT(GaugeWidgetConfig)
    CONFIG_EDITABLE_VALUE(int, outerPadding, "outer_padding");
    CONFIG_EDITABLE_VALUE(int, ringThickness, "ring_thickness");
    CONFIG_EDITABLE_VALUE_WITH(double, sweepDegrees, "sweep_degrees", configschema::GaugeSweepDegreesPolicy);
    CONFIG_EDITABLE_VALUE(int, segmentCount, "segment_count");
    CONFIG_EDITABLE_VALUE_WITH(
        double, segmentGapDegrees, "segment_gap_degrees", configschema::GaugeSegmentGapDegreesPolicy);
    CONFIG_EDITABLE_VALUE(int, valueBottom, "value_bottom");
    CONFIG_EDITABLE_VALUE(int, labelBottom, "label_bottom");
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

struct LayoutEditorConfig {
    CONFIG_REFLECTED_STRUCT(LayoutEditorConfig)
    CONFIG_VALUE(int, sizeSimilarityThreshold, "size_similarity_threshold");
    CONFIG_SECTION("layout_editor");
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
    CONFIG_SECTION_VALUE(LayoutEditorConfig, layoutEditor);
    CONFIG_SECTION_VALUE(UiFontSetConfig, fonts);
    CONFIG_DYNAMIC_SECTION_VALUE(LayoutCardConfig, cards, id);
    CONFIG_BINDING_LIST();

    LayoutSectionConfig structure{};
    LayoutNodeConfig cardsLayout;
};

struct AppConfig {
    CONFIG_REFLECTED_BINDINGS(AppConfig)
    CONFIG_SECTION_VALUE(DisplayConfig, display);
    CONFIG_SECTION_VALUE(NetworkConfig, network);
    CONFIG_SECTION_VALUE(StorageConfig, storage);
    CONFIG_SECTION_VALUE(BoardConfig, board);
    CONFIG_SECTION_VALUE(MetricScaleConfig, metricScales);
    CONFIG_DYNAMIC_SECTION_VALUE(LayoutSectionConfig, layouts, name);
    CONFIG_RECURSIVE_BINDING_VALUE(LayoutConfig, layout);
    CONFIG_BINDING_LIST();
};

CONFIG_EDITABLE_ROOT_BINDING_PATH(UiFontSetConfig, AppConfig, AppConfig::layoutBinding, LayoutConfig::fontsBinding);
CONFIG_EDITABLE_ROOT_BINDING_PATH(
    MetricListWidgetConfig, AppConfig, AppConfig::layoutBinding, LayoutConfig::metricListBinding);
CONFIG_EDITABLE_ROOT_BINDING_PATH(
    DriveUsageListWidgetConfig, AppConfig, AppConfig::layoutBinding, LayoutConfig::driveUsageListBinding);
CONFIG_EDITABLE_ROOT_BINDING_PATH(
    ThroughputWidgetConfig, AppConfig, AppConfig::layoutBinding, LayoutConfig::throughputBinding);
CONFIG_EDITABLE_ROOT_BINDING_PATH(GaugeWidgetConfig, AppConfig, AppConfig::layoutBinding, LayoutConfig::gaugeBinding);
