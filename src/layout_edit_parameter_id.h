#pragma once

enum class LayoutEditParameter {
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

    // Text
    TextBottomGap,
    NetworkFooterBottomGap,

    // Card style guides
    CardPadding,
    CardHeaderIconGap,
    CardHeaderContentGap,
    CardRowGap,
    CardColumnGap,

    // Dashboard guides
    DashboardRowGap,
    DashboardColumnGap,

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
    Count,
};
