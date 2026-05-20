#pragma once

#include "config/config_primitives.h"

// config_meta: static [display]
struct DisplayConfig {
    std::string monitorName{};
    std::string layout{};
    std::string theme{};
    std::string wallpaper{};
    LogicalPointConfig position{};
    double scale{};

    bool operator==(const DisplayConfig& other) const = default;
};

// config_meta: static [gpu]
struct GpuConfig {
    std::string adapterName{};

    bool operator==(const GpuConfig& other) const = default;
};

// config_meta: static [network]
struct NetworkConfig {
    std::string adapterName{};

    bool operator==(const NetworkConfig& other) const = default;
};

// config_meta: static [storage]
struct StorageConfig {
    std::vector<std::string> drives{};

    bool operator==(const StorageConfig& other) const = default;
};

// config_meta: dynamic_section [theme.$name] key=name
struct ThemeConfig {
    std::string name{};
    std::string description{};
    ColorConfig background{};
    ColorConfig foreground{};
    ColorConfig accent{};
    ColorConfig guide{};

    bool operator==(const ThemeConfig& other) const = default;
};

// config_meta: static [colors]
struct ColorsConfig {
    ColorConfig backgroundColor{};
    ColorConfig foregroundColor{};
    ColorConfig iconColor{};
    ColorConfig accentColor{};
    ColorConfig peakGhostColor{};
    ColorConfig warningColor{};
    ColorConfig layoutGuideColor{};
    ColorConfig activeEditColor{};
    ColorConfig panelBorderColor{};
    ColorConfig mutedTextColor{};
    ColorConfig trackColor{};
    ColorConfig panelFillColor{};
    ColorConfig graphBackgroundColor{};
    ColorConfig graphAxisColor{};
    ColorConfig graphMarkerColor{};

    bool operator==(const ColorsConfig& other) const = default;
};

// config_meta: static [layout_guide_sheet]
struct LayoutGuideSheetConfig {
    ColorConfig calloutLeaderColor{};
    ColorConfig calloutFillColor{};
    ColorConfig calloutBorderColor{};
    ColorConfig calloutParameterColor{};
    ColorConfig calloutDescriptionColor{};
    int sheetMargin{};
    int blockGap{};
    int calloutGap{};
    int calloutRowGap{};
    int calloutPaddingX{};
    int calloutPaddingY{};
    int calloutLineGap{};
    int calloutRadius{};
    int calloutBorderWidth{};
    int leaderStrokeWidth{};
    int leaderEndpointDiameter{};
    int overviewBorderWidth{};
    int overviewGuideStrokeWidth{};
    int overviewGuideHitInset{};
    int overviewGapHandleSize{};
    int overviewAnchorMaxSize{};
    int overviewDottedPadding{};
    int overviewDottedStrokeWidth{};
    int overviewDottedDashLength{};
    int overviewDottedGapLength{};

    bool operator==(const LayoutGuideSheetConfig& other) const = default;
};

// config_meta: static [dashboard]
struct DashboardSectionConfig {
    int outerMargin{};  // config_meta: policy=non_negative_int
    int rowGap{};       // config_meta: policy=non_negative_int
    int columnGap{};    // config_meta: policy=non_negative_int

    bool operator==(const DashboardSectionConfig& other) const = default;
};

// config_meta: static [card_style]
struct CardStyleConfig {
    int cardPadding{};  // config_meta: policy=non_negative_int
    int cardRadius{};
    int cardBorder{};
    int headerIconSize{};
    int headerIconGap{};     // config_meta: policy=non_negative_int
    int headerContentGap{};  // config_meta: policy=non_negative_int
    int rowGap{};            // config_meta: policy=non_negative_int
    int columnGap{};         // config_meta: policy=non_negative_int

    bool operator==(const CardStyleConfig& other) const = default;
};

// config_meta: static [fonts]
struct FontsConfig {
    UiFontConfig title{};
    UiFontConfig big{};
    UiFontConfig value{};
    UiFontConfig label{};
    UiFontConfig text{};
    UiFontConfig smallText{};  // config_meta: rename=small
    UiFontConfig footer{};
    UiFontConfig clockTime{};
    UiFontConfig clockDate{};

    bool operator==(const FontsConfig& other) const = default;
};

// config_meta: static [metric_list]
struct MetricListWidgetConfig {
    int labelWidth{};
    int barHeight{};
    int rowGap{};  // config_meta: policy=non_negative_int

    bool operator==(const MetricListWidgetConfig& other) const = default;
};

// config_meta: static [drive_usage_list]
struct DriveUsageListWidgetConfig {
    int labelGap{};  // config_meta: policy=non_negative_int
    int activityWidth{};
    int rwGap{};       // config_meta: policy=non_negative_int
    int barGap{};      // config_meta: policy=non_negative_int
    int percentGap{};  // config_meta: policy=non_negative_int
    int freeWidth{};
    int barHeight{};
    int headerGap{};  // config_meta: policy=non_negative_int
    int rowGap{};     // config_meta: policy=non_negative_int
    int activitySegments{};
    int activitySegmentGap{};  // config_meta: policy=non_negative_int

    bool operator==(const DriveUsageListWidgetConfig& other) const = default;
};

// config_meta: static [throughput]
struct ThroughputWidgetConfig {
    int headerGap{};    // config_meta: policy=non_negative_int
    int axisPadding{};  // config_meta: policy=non_negative_int
    int guideStrokeWidth{};
    int plotStrokeWidth{};
    int leaderDiameter{};

    bool operator==(const ThroughputWidgetConfig& other) const = default;
};

// config_meta: static [gauge]
struct GaugeWidgetConfig {
    int outerPadding{};  // config_meta: policy=non_negative_int
    int ringThickness{};
    double sweepDegrees{};  // config_meta: policy=degrees
    int segmentCount{};
    double segmentGapDegrees{};  // config_meta: policy=degrees
    int valueBottom{};
    int labelBottom{};

    bool operator==(const GaugeWidgetConfig& other) const = default;
};

// config_meta: static [text]
struct TextWidgetConfig {
    int bottomGap{};  // config_meta: policy=non_negative_int

    bool operator==(const TextWidgetConfig& other) const = default;
};

// config_meta: static [network_footer]
struct NetworkFooterWidgetConfig {
    int bottomGap{};  // config_meta: policy=non_negative_int

    bool operator==(const NetworkFooterWidgetConfig& other) const = default;
};

// config_meta: static [layout_editor]
struct LayoutEditorConfig {
    int sizeSimilarityThreshold{};

    bool operator==(const LayoutEditorConfig& other) const = default;
};

// config_meta: dynamic_section [layout.$name] key=name
struct LayoutSectionConfig {
    std::string name{};
    std::string description{};
    LogicalSizeConfig window{};
    LayoutNodeConfig cards{};

    bool operator==(const LayoutSectionConfig& other) const = default;
};

// config_meta: dynamic_section [card.$id] key=id
struct LayoutCardConfig {
    std::string id{};
    std::string title{};
    std::string icon{};
    LayoutNodeConfig layout{};

    bool operator==(const LayoutCardConfig& other) const = default;
};

// config_meta: container
struct LayoutConfig {
    ColorsConfig colors{};
    LayoutGuideSheetConfig layoutGuideSheet{};
    std::vector<ThemeConfig> themes{};
    DashboardSectionConfig dashboard{};
    CardStyleConfig cardStyle{};
    MetricListWidgetConfig metricList{};
    DriveUsageListWidgetConfig driveUsageList{};
    ThroughputWidgetConfig throughput{};
    GaugeWidgetConfig gauge{};
    TextWidgetConfig text{};
    NetworkFooterWidgetConfig networkFooter{};
    LayoutEditorConfig layoutEditor{};
    FontsConfig fonts{};
    BoardConfig board{};
    MetricsSectionConfig metrics{};
    std::vector<LayoutCardConfig> cards{};
    std::vector<LayoutSectionConfig> layouts{};

    LayoutSectionConfig structure{};  // config_meta: runtime_only

    bool operator==(const LayoutConfig& other) const = default;
};

// config_meta: root
struct AppConfig {
    DisplayConfig display{};
    GpuConfig gpu{};
    NetworkConfig network{};
    StorageConfig storage{};
    LayoutConfig layout{};

    bool operator==(const AppConfig& other) const = default;
};
