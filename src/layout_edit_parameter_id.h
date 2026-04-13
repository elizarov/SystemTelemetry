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

    // Text
    TextBottomPadding,

    // Metric list
    MetricListBarHeight,
    MetricListLabelWidth,
    MetricListVerticalGap,

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
