#pragma once

#include "config/config.h"

#define CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, section_type, field_member) X(name, section_type::field_member##Meta)

#define CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, name, field_member)                                                     \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, UiFontSetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, name, field_member)                                               \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, CardStyleConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_DASHBOARD_PARAMETER(X, name, field_member)                                                \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, DashboardSectionConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_TEXT_PARAMETER(X, name, field_member)                                                     \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, TextWidgetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_NETWORK_FOOTER_PARAMETER(X, name, field_member)                                           \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, NetworkFooterWidgetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_METRIC_LIST_PARAMETER(X, name, field_member)                                              \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, MetricListWidgetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, name, field_member)                                              \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, DriveUsageListWidgetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_THROUGHPUT_PARAMETER(X, name, field_member)                                               \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, ThroughputWidgetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, name, field_member)                                                    \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, GaugeWidgetConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, name, field_member)                                                    \
    CASEDASH_LAYOUT_EDIT_PARAMETER(X, name, ColorsConfig, field_member)

#define CASEDASH_LAYOUT_EDIT_PARAMETER_ITEMS(X)                                                                        \
    /* Fonts */                                                                                                        \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontTitle, title)                                                           \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontBig, big)                                                               \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontValue, value)                                                           \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontLabel, label)                                                           \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontText, text)                                                             \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontSmall, smallText)                                                       \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontFooter, footer)                                                         \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontClockTime, clockTime)                                                   \
    CASEDASH_LAYOUT_EDIT_FONT_PARAMETER(X, FontClockDate, clockDate)                                                   \
    /* Card style anchors */                                                                                           \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardRadius, cardRadius)                                               \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardBorder, cardBorderWidth)                                          \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardHeaderIconSize, headerIconSize)                                   \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardRowGap, rowGap)                                                   \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardColumnGap, columnGap)                                             \
    /* Card style guides */                                                                                            \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardPadding, cardPadding)                                             \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardHeaderIconGap, headerIconGap)                                     \
    CASEDASH_LAYOUT_EDIT_CARD_STYLE_PARAMETER(X, CardHeaderContentGap, headerContentGap)                               \
    /* Dashboard guides */                                                                                             \
    CASEDASH_LAYOUT_EDIT_DASHBOARD_PARAMETER(X, DashboardOuterMargin, outerMargin)                                     \
    CASEDASH_LAYOUT_EDIT_DASHBOARD_PARAMETER(X, DashboardRowGap, rowGap)                                               \
    CASEDASH_LAYOUT_EDIT_DASHBOARD_PARAMETER(X, DashboardColumnGap, columnGap)                                         \
    /* Text */                                                                                                         \
    CASEDASH_LAYOUT_EDIT_TEXT_PARAMETER(X, TextBottomGap, bottomGap)                                                   \
    CASEDASH_LAYOUT_EDIT_NETWORK_FOOTER_PARAMETER(X, NetworkFooterBottomGap, bottomGap)                                \
    /* Metric list */                                                                                                  \
    CASEDASH_LAYOUT_EDIT_METRIC_LIST_PARAMETER(X, MetricListBarHeight, barHeight)                                      \
    CASEDASH_LAYOUT_EDIT_METRIC_LIST_PARAMETER(X, MetricListLabelWidth, labelWidth)                                    \
    CASEDASH_LAYOUT_EDIT_METRIC_LIST_PARAMETER(X, MetricListRowGap, rowGap)                                            \
    /* Drive usage list */                                                                                             \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageActivitySegments, activitySegments)                        \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageBarHeight, barHeight)                                      \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageLabelGap, labelGap)                                        \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageBarGap, barGap)                                            \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageRwGap, rwGap)                                              \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsagePercentGap, percentGap)                                    \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageActivityWidth, activityWidth)                              \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageFreeWidth, freeWidth)                                      \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageActivitySegmentGap, activitySegmentGap)                    \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageHeaderGap, headerGap)                                      \
    CASEDASH_LAYOUT_EDIT_DRIVE_USAGE_PARAMETER(X, DriveUsageRowGap, rowGap)                                            \
    /* Throughput */                                                                                                   \
    CASEDASH_LAYOUT_EDIT_THROUGHPUT_PARAMETER(X, ThroughputGuideStrokeWidth, guideStrokeWidth)                         \
    CASEDASH_LAYOUT_EDIT_THROUGHPUT_PARAMETER(X, ThroughputPlotStrokeWidth, plotStrokeWidth)                           \
    CASEDASH_LAYOUT_EDIT_THROUGHPUT_PARAMETER(X, ThroughputLeaderDiameter, leaderDiameter)                             \
    CASEDASH_LAYOUT_EDIT_THROUGHPUT_PARAMETER(X, ThroughputAxisPadding, axisPadding)                                   \
    CASEDASH_LAYOUT_EDIT_THROUGHPUT_PARAMETER(X, ThroughputHeaderGap, headerGap)                                       \
    /* Gauge */                                                                                                        \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeSegmentCount, segmentCount)                                           \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeValueBottom, valueBottom)                                             \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeLabelBottom, labelBottom)                                             \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeSweepDegrees, sweepDegrees)                                           \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeSegmentGapDegrees, segmentGapDegrees)                                 \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeOuterPadding, outerPadding)                                           \
    CASEDASH_LAYOUT_EDIT_GAUGE_PARAMETER(X, GaugeRingThickness, ringThickness)                                         \
    /* Colors */                                                                                                       \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorBackground, backgroundColor)                                          \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorForeground, foregroundColor)                                          \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorIcon, iconColor)                                                      \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorPeakGhost, peakGhostColor)                                            \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorWarning, warningColor)                                                \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorAccent, accentColor)                                                  \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorLayoutGuide, layoutGuideColor)                                        \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorActiveEdit, activeEditColor)                                          \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorPanelBorder, panelBorderColor)                                        \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorMutedText, mutedTextColor)                                            \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorTrack, trackColor)                                                    \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorPanelFill, panelFillColor)                                            \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorGraphBackground, graphBackgroundColor)                                \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorGraphAxis, graphAxisColor)                                            \
    CASEDASH_LAYOUT_EDIT_COLOR_PARAMETER(X, ColorGraphMarker, graphMarkerColor)

#define CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_ENUM(name, meta) name,

enum class LayoutEditParameter {
    // Hit-testing priority follows this declaration order for actionable widget-local handles and guides.
    CASEDASH_LAYOUT_EDIT_PARAMETER_ITEMS(CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_ENUM) Count,
};

#undef CASEDASH_DECLARE_LAYOUT_EDIT_PARAMETER_ENUM
