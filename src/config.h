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
    UiFontConfig title{};
    UiFontConfig big{};
    UiFontConfig value{};
    UiFontConfig label{};
    UiFontConfig smallText{};

    CONFIG_SECTION(UiFontSetConfig, "fonts",
        CONFIG_FIELD(UiFontSetConfig, title, "title", configschema::FontSpecCodec),
        CONFIG_FIELD(UiFontSetConfig, big, "big", configschema::FontSpecCodec),
        CONFIG_FIELD(UiFontSetConfig, value, "value", configschema::FontSpecCodec),
        CONFIG_FIELD(UiFontSetConfig, label, "label", configschema::FontSpecCodec),
        CONFIG_FIELD(UiFontSetConfig, smallText, "small", configschema::FontSpecCodec)
    );
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
    std::string monitorName;
    std::string wallpaper;
    LogicalPointConfig position{};

    CONFIG_SECTION(DisplayConfig, "display",
        CONFIG_FIELD(DisplayConfig, monitorName, "monitor_name", configschema::StringCodec),
        CONFIG_FIELD(DisplayConfig, wallpaper, "wallpaper", configschema::StringCodec),
        CONFIG_FIELD(DisplayConfig, position, "position", configschema::LogicalPointCodec)
    );
};

struct NetworkConfig {
    std::string adapterName;

    CONFIG_SECTION(NetworkConfig, "network",
        CONFIG_FIELD(NetworkConfig, adapterName, "adapter_name", configschema::StringCodec)
    );
};

struct DashboardSectionConfig {
    int outerMargin = 0;
    int rowGap = 0;
    int cardGap = 0;

    CONFIG_SECTION(DashboardSectionConfig, "dashboard",
        CONFIG_FIELD(DashboardSectionConfig, outerMargin, "outer_margin", configschema::IntCodec),
        CONFIG_FIELD(DashboardSectionConfig, rowGap, "row_gap", configschema::IntCodec),
        CONFIG_FIELD(DashboardSectionConfig, cardGap, "card_gap", configschema::IntCodec)
    );
};

struct CardStyleConfig {
    int cardPadding = 0;
    int cardRadius = 0;
    int cardBorderWidth = 0;
    int headerHeight = 0;
    int headerIconSize = 0;
    int headerGap = 0;
    int contentGap = 0;
    int columnGap = 0;
    int widgetLineGap = 0;

    CONFIG_SECTION(CardStyleConfig, "card_style",
        CONFIG_FIELD(CardStyleConfig, cardPadding, "card_padding", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, cardRadius, "card_radius", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, cardBorderWidth, "card_border", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, headerHeight, "header_height", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, headerIconSize, "header_icon_size", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, headerGap, "header_gap", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, contentGap, "content_gap", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, columnGap, "column_gap", configschema::IntCodec),
        CONFIG_FIELD(CardStyleConfig, widgetLineGap, "widget_line_gap", configschema::IntCodec)
    );
};

struct ColorConfig {
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

    CONFIG_SECTION(ColorConfig, "colors",
        CONFIG_FIELD(ColorConfig, backgroundColor, "background_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, foregroundColor, "foreground_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, accentColor, "accent_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, panelBorderColor, "panel_border_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, mutedTextColor, "muted_text_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, trackColor, "track_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, panelFillColor, "panel_fill_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, graphBackgroundColor, "graph_background_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, graphGridColor, "graph_grid_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, graphAxisColor, "graph_axis_color", configschema::HexColorCodec),
        CONFIG_FIELD(ColorConfig, graphMarkerColor, "graph_marker_color", configschema::HexColorCodec)
    );
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::string parameter;
    std::vector<LayoutNodeConfig> children;
};

struct LayoutSectionConfig {
    LogicalSizeConfig window{};
    LayoutNodeConfig cardsLayout;

    CONFIG_SECTION(LayoutSectionConfig, "layout",
        CONFIG_FIELD(LayoutSectionConfig, window, "window", configschema::LogicalSizeCodec),
        CONFIG_FIELD(LayoutSectionConfig, cardsLayout, "cards", configschema::LayoutExpressionCodec)
    );
};

struct LayoutCardConfig {
    std::string id;
    std::string title;
    std::string icon;
    LayoutNodeConfig layout;

    CONFIG_DYNAMIC_SECTION(LayoutCardConfig, "card.",
        CONFIG_FIELD(LayoutCardConfig, title, "title", configschema::StringCodec),
        CONFIG_FIELD(LayoutCardConfig, icon, "icon", configschema::StringCodec),
        CONFIG_FIELD(LayoutCardConfig, layout, "layout", configschema::LayoutExpressionCodec)
    );
};

struct MetricScaleConfig {
    double cpuClockGHz = 0.0;
    double gpuTemperatureC = 0.0;
    double gpuClockMHz = 0.0;
    double gpuFanRpm = 0.0;
    double boardTemperatureC = 0.0;
    double boardFanRpm = 0.0;

    CONFIG_SECTION(MetricScaleConfig, "metric_scales",
        CONFIG_FIELD(MetricScaleConfig, cpuClockGHz, "cpu_clock_ghz", configschema::DoubleCodec),
        CONFIG_FIELD(MetricScaleConfig, gpuTemperatureC, "gpu_temperature_c", configschema::DoubleCodec),
        CONFIG_FIELD(MetricScaleConfig, gpuClockMHz, "gpu_clock_mhz", configschema::DoubleCodec),
        CONFIG_FIELD(MetricScaleConfig, gpuFanRpm, "gpu_fan_rpm", configschema::DoubleCodec),
        CONFIG_FIELD(MetricScaleConfig, boardTemperatureC, "board_temperature_c", configschema::DoubleCodec),
        CONFIG_FIELD(MetricScaleConfig, boardFanRpm, "board_fan_rpm", configschema::DoubleCodec)
    );
};

struct MetricListWidgetConfig {
    int labelWidth = 0;
    int valueGap = 0;
    int barHeight = 0;
    int verticalGap = 0;

    CONFIG_SECTION(MetricListWidgetConfig, "metric_list",
        CONFIG_FIELD(MetricListWidgetConfig, labelWidth, "label_width", configschema::IntCodec),
        CONFIG_FIELD(MetricListWidgetConfig, valueGap, "value_gap", configschema::IntCodec),
        CONFIG_FIELD(MetricListWidgetConfig, barHeight, "bar_height", configschema::IntCodec),
        CONFIG_FIELD(MetricListWidgetConfig, verticalGap, "vertical_gap", configschema::IntCodec)
    );
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

    CONFIG_SECTION(DriveUsageListWidgetConfig, "drive_usage_list",
        CONFIG_FIELD(DriveUsageListWidgetConfig, freeWidth, "free_width", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, activityWidth, "activity_width", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, barGap, "bar_gap", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, valueGap, "value_gap", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, percentGap, "percent_gap", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, barHeight, "bar_height", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, verticalGap, "vertical_gap", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, labelPadding, "label_padding", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, percentPadding, "percent_padding", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, activitySegments, "activity_segments", configschema::IntCodec),
        CONFIG_FIELD(DriveUsageListWidgetConfig, activitySegmentGap, "activity_segment_gap", configschema::IntCodec)
    );
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

    CONFIG_SECTION(ThroughputWidgetConfig, "throughput",
        CONFIG_FIELD(ThroughputWidgetConfig, headerGap, "header_gap", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, graphHeight, "graph_height", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, valuePadding, "value_padding", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, labelPadding, "label_padding", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, axisPadding, "axis_padding", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, scaleLabelPadding, "scale_label_padding", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, scaleLabelMinHeight, "scale_label_min_height", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, guideStrokeWidth, "guide_stroke_width", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, plotStrokeWidth, "plot_stroke_width", configschema::IntCodec),
        CONFIG_FIELD(ThroughputWidgetConfig, leaderDiameter, "leader_diameter", configschema::IntCodec)
    );
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

    CONFIG_SECTION(GaugeWidgetConfig, "gauge",
        CONFIG_FIELD(GaugeWidgetConfig, preferredSize, "preferred_size", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, outerPadding, "outer_padding", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, minRadius, "min_radius", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, ringThickness, "ring_thickness", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, sweepDegrees, "sweep_degrees", configschema::DoubleCodec),
        CONFIG_FIELD(GaugeWidgetConfig, segmentCount, "segment_count", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, segmentGapDegrees, "segment_gap_degrees", configschema::DoubleCodec),
        CONFIG_FIELD(GaugeWidgetConfig, textHalfWidth, "text_half_width", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, valueTop, "value_top", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, valueBottom, "value_bottom", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, labelTop, "label_top", configschema::IntCodec),
        CONFIG_FIELD(GaugeWidgetConfig, labelBottom, "label_bottom", configschema::IntCodec)
    );
};

struct TextWidgetConfig {
    int preferredPadding = 0;

    CONFIG_SECTION(TextWidgetConfig, "text",
        CONFIG_FIELD(TextWidgetConfig, preferredPadding, "preferred_padding", configschema::IntCodec)
    );
};

struct NetworkFooterWidgetConfig {
    int preferredPadding = 0;

    CONFIG_SECTION(NetworkFooterWidgetConfig, "network_footer",
        CONFIG_FIELD(NetworkFooterWidgetConfig, preferredPadding, "preferred_padding", configschema::IntCodec)
    );
};

struct ClockTimeWidgetConfig {
    int padding = 0;

    CONFIG_SECTION(ClockTimeWidgetConfig, "clock_time",
        CONFIG_FIELD(ClockTimeWidgetConfig, padding, "padding", configschema::IntCodec)
    );
};

struct ClockDateWidgetConfig {
    int padding = 0;

    CONFIG_SECTION(ClockDateWidgetConfig, "clock_date",
        CONFIG_FIELD(ClockDateWidgetConfig, padding, "padding", configschema::IntCodec)
    );
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
