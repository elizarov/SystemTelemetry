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

    using Section = configschema::SectionDescriptor<"fonts", UiFontSetConfig,
        configschema::FieldDescriptor<UiFontSetConfig, UiFontConfig, "title", &UiFontSetConfig::title, configschema::FontSpecCodec>,
        configschema::FieldDescriptor<UiFontSetConfig, UiFontConfig, "big", &UiFontSetConfig::big, configschema::FontSpecCodec>,
        configschema::FieldDescriptor<UiFontSetConfig, UiFontConfig, "value", &UiFontSetConfig::value, configschema::FontSpecCodec>,
        configschema::FieldDescriptor<UiFontSetConfig, UiFontConfig, "label", &UiFontSetConfig::label, configschema::FontSpecCodec>,
        configschema::FieldDescriptor<UiFontSetConfig, UiFontConfig, "small", &UiFontSetConfig::smallText, configschema::FontSpecCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"display", DisplayConfig,
        configschema::FieldDescriptor<DisplayConfig, std::string, "monitor_name", &DisplayConfig::monitorName, configschema::StringCodec>,
        configschema::FieldDescriptor<DisplayConfig, std::string, "wallpaper", &DisplayConfig::wallpaper, configschema::StringCodec>,
        configschema::FieldDescriptor<DisplayConfig, LogicalPointConfig, "position", &DisplayConfig::position, configschema::LogicalPointCodec>
    >;
};

struct NetworkConfig {
    std::string adapterName;

    using Section = configschema::SectionDescriptor<"network", NetworkConfig,
        configschema::FieldDescriptor<NetworkConfig, std::string, "adapter_name", &NetworkConfig::adapterName, configschema::StringCodec>
    >;
};

struct DashboardSectionConfig {
    int outerMargin = 0;
    int rowGap = 0;
    int cardGap = 0;

    using Section = configschema::SectionDescriptor<"dashboard", DashboardSectionConfig,
        configschema::FieldDescriptor<DashboardSectionConfig, int, "outer_margin", &DashboardSectionConfig::outerMargin, configschema::IntCodec>,
        configschema::FieldDescriptor<DashboardSectionConfig, int, "row_gap", &DashboardSectionConfig::rowGap, configschema::IntCodec>,
        configschema::FieldDescriptor<DashboardSectionConfig, int, "card_gap", &DashboardSectionConfig::cardGap, configschema::IntCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"card_style", CardStyleConfig,
        configschema::FieldDescriptor<CardStyleConfig, int, "card_padding", &CardStyleConfig::cardPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "card_radius", &CardStyleConfig::cardRadius, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "card_border", &CardStyleConfig::cardBorderWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "header_height", &CardStyleConfig::headerHeight, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "header_icon_size", &CardStyleConfig::headerIconSize, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "header_gap", &CardStyleConfig::headerGap, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "content_gap", &CardStyleConfig::contentGap, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "column_gap", &CardStyleConfig::columnGap, configschema::IntCodec>,
        configschema::FieldDescriptor<CardStyleConfig, int, "widget_line_gap", &CardStyleConfig::widgetLineGap, configschema::IntCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"colors", ColorConfig,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "background_color", &ColorConfig::backgroundColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "foreground_color", &ColorConfig::foregroundColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "accent_color", &ColorConfig::accentColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "panel_border_color", &ColorConfig::panelBorderColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "muted_text_color", &ColorConfig::mutedTextColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "track_color", &ColorConfig::trackColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "panel_fill_color", &ColorConfig::panelFillColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "graph_background_color", &ColorConfig::graphBackgroundColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "graph_grid_color", &ColorConfig::graphGridColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "graph_axis_color", &ColorConfig::graphAxisColor, configschema::HexColorCodec>,
        configschema::FieldDescriptor<ColorConfig, unsigned int, "graph_marker_color", &ColorConfig::graphMarkerColor, configschema::HexColorCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"layout", LayoutSectionConfig,
        configschema::FieldDescriptor<LayoutSectionConfig, LogicalSizeConfig, "window", &LayoutSectionConfig::window, configschema::LogicalSizeCodec>,
        configschema::FieldDescriptor<LayoutSectionConfig, LayoutNodeConfig, "cards", &LayoutSectionConfig::cardsLayout, configschema::LayoutExpressionCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"metric_scales", MetricScaleConfig,
        configschema::FieldDescriptor<MetricScaleConfig, double, "cpu_clock_ghz", &MetricScaleConfig::cpuClockGHz, configschema::DoubleCodec>,
        configschema::FieldDescriptor<MetricScaleConfig, double, "gpu_temperature_c", &MetricScaleConfig::gpuTemperatureC, configschema::DoubleCodec>,
        configschema::FieldDescriptor<MetricScaleConfig, double, "gpu_clock_mhz", &MetricScaleConfig::gpuClockMHz, configschema::DoubleCodec>,
        configschema::FieldDescriptor<MetricScaleConfig, double, "gpu_fan_rpm", &MetricScaleConfig::gpuFanRpm, configschema::DoubleCodec>,
        configschema::FieldDescriptor<MetricScaleConfig, double, "board_temperature_c", &MetricScaleConfig::boardTemperatureC, configschema::DoubleCodec>,
        configschema::FieldDescriptor<MetricScaleConfig, double, "board_fan_rpm", &MetricScaleConfig::boardFanRpm, configschema::DoubleCodec>
    >;
};

struct MetricListWidgetConfig {
    int labelWidth = 0;
    int valueGap = 0;
    int barHeight = 0;
    int verticalGap = 0;

    using Section = configschema::SectionDescriptor<"metric_list", MetricListWidgetConfig,
        configschema::FieldDescriptor<MetricListWidgetConfig, int, "label_width", &MetricListWidgetConfig::labelWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<MetricListWidgetConfig, int, "value_gap", &MetricListWidgetConfig::valueGap, configschema::IntCodec>,
        configschema::FieldDescriptor<MetricListWidgetConfig, int, "bar_height", &MetricListWidgetConfig::barHeight, configschema::IntCodec>,
        configschema::FieldDescriptor<MetricListWidgetConfig, int, "vertical_gap", &MetricListWidgetConfig::verticalGap, configschema::IntCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"drive_usage_list", DriveUsageListWidgetConfig,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "free_width", &DriveUsageListWidgetConfig::freeWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "activity_width", &DriveUsageListWidgetConfig::activityWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "bar_gap", &DriveUsageListWidgetConfig::barGap, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "value_gap", &DriveUsageListWidgetConfig::valueGap, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "percent_gap", &DriveUsageListWidgetConfig::percentGap, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "bar_height", &DriveUsageListWidgetConfig::barHeight, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "vertical_gap", &DriveUsageListWidgetConfig::verticalGap, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "label_padding", &DriveUsageListWidgetConfig::labelPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "percent_padding", &DriveUsageListWidgetConfig::percentPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "activity_segments", &DriveUsageListWidgetConfig::activitySegments, configschema::IntCodec>,
        configschema::FieldDescriptor<DriveUsageListWidgetConfig, int, "activity_segment_gap", &DriveUsageListWidgetConfig::activitySegmentGap, configschema::IntCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"throughput", ThroughputWidgetConfig,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "header_gap", &ThroughputWidgetConfig::headerGap, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "graph_height", &ThroughputWidgetConfig::graphHeight, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "value_padding", &ThroughputWidgetConfig::valuePadding, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "label_padding", &ThroughputWidgetConfig::labelPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "axis_padding", &ThroughputWidgetConfig::axisPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "scale_label_padding", &ThroughputWidgetConfig::scaleLabelPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "scale_label_min_height", &ThroughputWidgetConfig::scaleLabelMinHeight, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "guide_stroke_width", &ThroughputWidgetConfig::guideStrokeWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "plot_stroke_width", &ThroughputWidgetConfig::plotStrokeWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<ThroughputWidgetConfig, int, "leader_diameter", &ThroughputWidgetConfig::leaderDiameter, configschema::IntCodec>
    >;
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

    using Section = configschema::SectionDescriptor<"gauge", GaugeWidgetConfig,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "preferred_size", &GaugeWidgetConfig::preferredSize, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "outer_padding", &GaugeWidgetConfig::outerPadding, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "min_radius", &GaugeWidgetConfig::minRadius, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "ring_thickness", &GaugeWidgetConfig::ringThickness, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, double, "sweep_degrees", &GaugeWidgetConfig::sweepDegrees, configschema::DoubleCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "segment_count", &GaugeWidgetConfig::segmentCount, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, double, "segment_gap_degrees", &GaugeWidgetConfig::segmentGapDegrees, configschema::DoubleCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "text_half_width", &GaugeWidgetConfig::textHalfWidth, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "value_top", &GaugeWidgetConfig::valueTop, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "value_bottom", &GaugeWidgetConfig::valueBottom, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "label_top", &GaugeWidgetConfig::labelTop, configschema::IntCodec>,
        configschema::FieldDescriptor<GaugeWidgetConfig, int, "label_bottom", &GaugeWidgetConfig::labelBottom, configschema::IntCodec>
    >;
};

struct TextWidgetConfig {
    int preferredPadding = 0;

    using Section = configschema::SectionDescriptor<"text", TextWidgetConfig,
        configschema::FieldDescriptor<TextWidgetConfig, int, "preferred_padding", &TextWidgetConfig::preferredPadding, configschema::IntCodec>
    >;
};

struct NetworkFooterWidgetConfig {
    int preferredPadding = 0;

    using Section = configschema::SectionDescriptor<"network_footer", NetworkFooterWidgetConfig,
        configschema::FieldDescriptor<NetworkFooterWidgetConfig, int, "preferred_padding", &NetworkFooterWidgetConfig::preferredPadding, configschema::IntCodec>
    >;
};

struct ClockTimeWidgetConfig {
    int padding = 0;

    using Section = configschema::SectionDescriptor<"clock_time", ClockTimeWidgetConfig,
        configschema::FieldDescriptor<ClockTimeWidgetConfig, int, "padding", &ClockTimeWidgetConfig::padding, configschema::IntCodec>
    >;
};

struct ClockDateWidgetConfig {
    int padding = 0;

    using Section = configschema::SectionDescriptor<"clock_date", ClockDateWidgetConfig,
        configschema::FieldDescriptor<ClockDateWidgetConfig, int, "padding", &ClockDateWidgetConfig::padding, configschema::IntCodec>
    >;
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
