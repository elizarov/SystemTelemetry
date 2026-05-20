#pragma once

#include <cstdint>

// config_meta: layout_enum
enum class LayoutEditParameter : std::uint8_t {
    // Hit-testing priority follows this declaration order for actionable widget-local handles and guides.
    // Fonts
    FontTitle,
    FontBig,
    FontValue,
    FontLabel,
    FontText,
    FontSmall,
    FontFooter,
    FontClockTime,
    FontClockDate,

    // Card style anchors
    CardRadius,
    CardBorder,
    CardHeaderIconSize,
    CardRowGap,
    CardColumnGap,

    // Card style guides
    CardPadding,
    CardHeaderIconGap,
    CardHeaderContentGap,

    // Dashboard guides
    DashboardOuterMargin,
    DashboardRowGap,
    DashboardColumnGap,

    // Text
    TextBottomGap,
    NetworkFooterBottomGap,

    // Metric list
    MetricListBarHeight,
    MetricListLabelWidth,
    MetricListRowGap,

    // Drive usage list
    DriveUsageActivitySegments,
    DriveUsageBarHeight,
    DriveUsageLabelGap,
    DriveUsageBarGap,
    DriveUsageRwGap,
    DriveUsagePercentGap,
    DriveUsageActivityWidth,
    DriveUsageFreeWidth,
    DriveUsageActivitySegmentGap,
    DriveUsageHeaderGap,
    DriveUsageRowGap,

    // Throughput
    ThroughputGuideStrokeWidth,
    ThroughputPlotStrokeWidth,
    ThroughputLeaderDiameter,
    ThroughputAxisPadding,
    ThroughputHeaderGap,

    // Gauge
    GaugeSegmentCount,
    GaugeValueBottom,
    GaugeLabelBottom,
    GaugeSweepDegrees,
    GaugeSegmentGapDegrees,
    GaugeOuterPadding,
    GaugeRingThickness,

    // Colors
    ColorBackground,
    ColorForeground,
    ColorIcon,
    ColorPeakGhost,
    ColorWarning,
    ColorAccent,
    ColorLayoutGuide,
    ColorActiveEdit,
    ColorPanelBorder,
    ColorMutedText,
    ColorTrack,
    ColorPanelFill,
    ColorGraphBackground,
    ColorGraphAxis,
    ColorGraphMarker,
    Count,
};
