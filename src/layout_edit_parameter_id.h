#pragma once

#include "config.h"

#define SYSTEM_TELEMETRY_LAYOUT_EDIT_PARAMETER_ITEMS(X)                                                                \
    /* Fonts */                                                                                                        \
    X(FontTitle, UiFontSetConfig::titleMeta)                                                                           \
    X(FontBig, UiFontSetConfig::bigMeta)                                                                               \
    X(FontValue, UiFontSetConfig::valueMeta)                                                                           \
    X(FontLabel, UiFontSetConfig::labelMeta)                                                                           \
    X(FontText, UiFontSetConfig::textMeta)                                                                             \
    X(FontSmall, UiFontSetConfig::smallTextMeta)                                                                       \
    X(FontFooter, UiFontSetConfig::footerMeta)                                                                         \
    X(FontClockTime, UiFontSetConfig::clockTimeMeta)                                                                   \
    X(FontClockDate, UiFontSetConfig::clockDateMeta)                                                                   \
    /* Card style anchors */                                                                                           \
    X(CardRadius, CardStyleConfig::cardRadiusMeta)                                                                     \
    X(CardBorder, CardStyleConfig::cardBorderWidthMeta)                                                                \
    X(CardHeaderIconSize, CardStyleConfig::headerIconSizeMeta)                                                         \
    /* Card style guides */                                                                                            \
    X(CardPadding, CardStyleConfig::cardPaddingMeta)                                                                   \
    X(CardHeaderIconGap, CardStyleConfig::headerIconGapMeta)                                                           \
    X(CardHeaderContentGap, CardStyleConfig::headerContentGapMeta)                                                     \
    X(CardRowGap, CardStyleConfig::rowGapMeta)                                                                         \
    X(CardColumnGap, CardStyleConfig::columnGapMeta)                                                                   \
    /* Dashboard guides */                                                                                             \
    X(DashboardOuterMargin, DashboardSectionConfig::outerMarginMeta)                                                   \
    X(DashboardRowGap, DashboardSectionConfig::rowGapMeta)                                                             \
    X(DashboardColumnGap, DashboardSectionConfig::columnGapMeta)                                                       \
    /* Text */                                                                                                         \
    X(TextBottomGap, TextWidgetConfig::bottomGapMeta)                                                                  \
    X(NetworkFooterBottomGap, NetworkFooterWidgetConfig::bottomGapMeta)                                                \
    /* Metric list */                                                                                                  \
    X(MetricListBarHeight, MetricListWidgetConfig::barHeightMeta)                                                      \
    X(MetricListLabelWidth, MetricListWidgetConfig::labelWidthMeta)                                                    \
    X(MetricListRowGap, MetricListWidgetConfig::rowGapMeta)                                                            \
    /* Drive usage list */                                                                                             \
    X(DriveUsageActivitySegments, DriveUsageListWidgetConfig::activitySegmentsMeta)                                    \
    X(DriveUsageBarHeight, DriveUsageListWidgetConfig::barHeightMeta)                                                  \
    X(DriveUsageLabelGap, DriveUsageListWidgetConfig::labelGapMeta)                                                    \
    X(DriveUsageBarGap, DriveUsageListWidgetConfig::barGapMeta)                                                        \
    X(DriveUsageRwGap, DriveUsageListWidgetConfig::rwGapMeta)                                                          \
    X(DriveUsagePercentGap, DriveUsageListWidgetConfig::percentGapMeta)                                                \
    X(DriveUsageActivityWidth, DriveUsageListWidgetConfig::activityWidthMeta)                                          \
    X(DriveUsageFreeWidth, DriveUsageListWidgetConfig::freeWidthMeta)                                                  \
    X(DriveUsageActivitySegmentGap, DriveUsageListWidgetConfig::activitySegmentGapMeta)                                \
    X(DriveUsageHeaderGap, DriveUsageListWidgetConfig::headerGapMeta)                                                  \
    X(DriveUsageRowGap, DriveUsageListWidgetConfig::rowGapMeta)                                                        \
    /* Throughput */                                                                                                   \
    X(ThroughputGuideStrokeWidth, ThroughputWidgetConfig::guideStrokeWidthMeta)                                        \
    X(ThroughputPlotStrokeWidth, ThroughputWidgetConfig::plotStrokeWidthMeta)                                          \
    X(ThroughputLeaderDiameter, ThroughputWidgetConfig::leaderDiameterMeta)                                            \
    X(ThroughputAxisPadding, ThroughputWidgetConfig::axisPaddingMeta)                                                  \
    X(ThroughputHeaderGap, ThroughputWidgetConfig::headerGapMeta)                                                      \
    /* Gauge */                                                                                                        \
    X(GaugeSegmentCount, GaugeWidgetConfig::segmentCountMeta)                                                          \
    X(GaugeValueBottom, GaugeWidgetConfig::valueBottomMeta)                                                            \
    X(GaugeLabelBottom, GaugeWidgetConfig::labelBottomMeta)                                                            \
    X(GaugeSweepDegrees, GaugeWidgetConfig::sweepDegreesMeta)                                                          \
    X(GaugeSegmentGapDegrees, GaugeWidgetConfig::segmentGapDegreesMeta)                                                \
    X(GaugeOuterPadding, GaugeWidgetConfig::outerPaddingMeta)                                                          \
    X(GaugeRingThickness, GaugeWidgetConfig::ringThicknessMeta)                                                        \
    /* Colors */                                                                                                       \
    X(ColorBackground, ColorsConfig::backgroundColorMeta)                                                              \
    X(ColorForeground, ColorsConfig::foregroundColorMeta)                                                              \
    X(ColorIcon, ColorsConfig::iconColorMeta)                                                                          \
    X(ColorAccent, ColorsConfig::accentColorMeta)                                                                      \
    X(ColorLayoutGuide, ColorsConfig::layoutGuideColorMeta)                                                            \
    X(ColorActiveEdit, ColorsConfig::activeEditColorMeta)                                                              \
    X(ColorPanelBorder, ColorsConfig::panelBorderColorMeta)                                                            \
    X(ColorMutedText, ColorsConfig::mutedTextColorMeta)                                                                \
    X(ColorTrack, ColorsConfig::trackColorMeta)                                                                        \
    X(ColorPanelFill, ColorsConfig::panelFillColorMeta)                                                                \
    X(ColorGraphBackground, ColorsConfig::graphBackgroundColorMeta)                                                    \
    X(ColorGraphAxis, ColorsConfig::graphAxisColorMeta)                                                                \
    X(ColorGraphMarker, ColorsConfig::graphMarkerColorMeta)

#define SYSTEM_TELEMETRY_DECLARE_LAYOUT_EDIT_PARAMETER_ENUM(name, meta) name,

enum class LayoutEditParameter {
    // Hit-testing priority follows this declaration order for actionable widget-local handles and guides.
    SYSTEM_TELEMETRY_LAYOUT_EDIT_PARAMETER_ITEMS(SYSTEM_TELEMETRY_DECLARE_LAYOUT_EDIT_PARAMETER_ENUM) Count,
};

#undef SYSTEM_TELEMETRY_DECLARE_LAYOUT_EDIT_PARAMETER_ENUM
