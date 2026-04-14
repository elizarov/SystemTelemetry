#include <gtest/gtest.h>

#include "layout_edit_commands.h"

TEST(LayoutEditCommands, ClampsGaugeDegreeFieldsToZeroTo360) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeSegmentGapDegrees, 100.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeSweepDegrees, 500.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeSegmentGapDegrees, -1.0));

    EXPECT_EQ(config.layout.gauge.sweepDegrees, 360.0);
    EXPECT_EQ(config.layout.gauge.segmentGapDegrees, 0.0);
}

TEST(LayoutEditCommands, ClampsFontSizesToPositiveValues) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::FontFooter, 0.2));

    EXPECT_EQ(config.layout.fonts.footer.size, 1);
}

TEST(LayoutEditCommands, AllowsZeroForEditablePaddingAndGapFields) {
    AppConfig config;
    config.layout.fonts.label.size = 15;
    config.layout.fonts.smallText.size = 13;
    config.layout.driveUsageList.barHeight = 10;
    config.layout.driveUsageList.activitySegments = 4;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::TextBottomGap, -3.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::NetworkFooterBottomGap, -3.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::MetricListRowGap, -2.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageLabelGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageRwGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageBarGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsagePercentGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageHeaderGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageRowGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageActivitySegmentGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::ThroughputAxisPadding, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::ThroughputHeaderGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeOuterPadding, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardPadding, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardHeaderIconGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardHeaderContentGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardRowGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardColumnGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DashboardOuterMargin, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DashboardRowGap, -1.0));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DashboardColumnGap, -1.0));

    EXPECT_EQ(config.layout.text.bottomGap, 0);
    EXPECT_EQ(config.layout.networkFooter.bottomGap, 0);
    EXPECT_EQ(config.layout.metricList.rowGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.labelGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.rwGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.barGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.percentGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.headerGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.rowGap, 0);
    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 0);
    EXPECT_EQ(config.layout.throughput.axisPadding, 0);
    EXPECT_EQ(config.layout.throughput.headerGap, 0);
    EXPECT_EQ(config.layout.gauge.outerPadding, 0);
    EXPECT_EQ(config.layout.cardStyle.cardPadding, 0);
    EXPECT_EQ(config.layout.cardStyle.headerIconGap, 0);
    EXPECT_EQ(config.layout.cardStyle.headerContentGap, 0);
    EXPECT_EQ(config.layout.cardStyle.rowGap, 0);
    EXPECT_EQ(config.layout.cardStyle.columnGap, 0);
    EXPECT_EQ(config.layout.dashboard.outerMargin, 0);
    EXPECT_EQ(config.layout.dashboard.rowGap, 0);
    EXPECT_EQ(config.layout.dashboard.columnGap, 0);
}

TEST(LayoutEditCommands, UpdatesMetricListAndGaugeFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::MetricListBarHeight, 17.2));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeSegmentCount, 6.4));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeOuterPadding, 9.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeRingThickness, 11.2));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeValueBottom, 17.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::GaugeLabelBottom, 33.2));

    EXPECT_EQ(config.layout.metricList.barHeight, 17);
    EXPECT_EQ(config.layout.gauge.segmentCount, 6);
    EXPECT_EQ(config.layout.gauge.outerPadding, 10);
    EXPECT_EQ(config.layout.gauge.ringThickness, 11);
    EXPECT_EQ(config.layout.gauge.valueBottom, 18);
    EXPECT_EQ(config.layout.gauge.labelBottom, 33);
}

TEST(LayoutEditCommands, UpdatesDriveUsageActivitySegmentsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageActivitySegments, 5.6));

    EXPECT_EQ(config.layout.driveUsageList.activitySegments, 6);
}

TEST(LayoutEditCommands, UpdatesThroughputAnchorFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::ThroughputGuideStrokeWidth, 3.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::ThroughputPlotStrokeWidth, 5.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::ThroughputLeaderDiameter, 8.6));

    EXPECT_EQ(config.layout.throughput.guideStrokeWidth, 4);
    EXPECT_EQ(config.layout.throughput.plotStrokeWidth, 6);
    EXPECT_EQ(config.layout.throughput.leaderDiameter, 9);
}

TEST(LayoutEditCommands, UpdatesCardStyleFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardRadius, 13.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardBorder, 2.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardHeaderIconSize, 19.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardPadding, 12.4));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardHeaderIconGap, 7.4));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardHeaderContentGap, 5.4));

    EXPECT_EQ(config.layout.cardStyle.cardRadius, 14);
    EXPECT_EQ(config.layout.cardStyle.cardBorderWidth, 3);
    EXPECT_EQ(config.layout.cardStyle.headerIconSize, 20);
    EXPECT_EQ(config.layout.cardStyle.cardPadding, 12);
    EXPECT_EQ(config.layout.cardStyle.headerIconGap, 7);
    EXPECT_EQ(config.layout.cardStyle.headerContentGap, 5);
}

TEST(LayoutEditCommands, UpdatesSharedContainerGapFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardRowGap, 3.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::CardColumnGap, 8.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DashboardOuterMargin, 5.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DashboardRowGap, 9.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DashboardColumnGap, 11.6));

    EXPECT_EQ(config.layout.cardStyle.rowGap, 4);
    EXPECT_EQ(config.layout.cardStyle.columnGap, 9);
    EXPECT_EQ(config.layout.dashboard.outerMargin, 6);
    EXPECT_EQ(config.layout.dashboard.rowGap, 10);
    EXPECT_EQ(config.layout.dashboard.columnGap, 12);
}

TEST(LayoutEditCommands, UpdatesDriveUsageGapFieldsThroughCommands) {
    AppConfig config;
    config.layout.fonts.label.size = 15;
    config.layout.fonts.smallText.size = 13;
    config.layout.driveUsageList.barHeight = 10;
    config.layout.driveUsageList.activitySegments = 4;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageLabelGap, 13.4));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageBarGap, 11.4));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageRwGap, 7.6));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsagePercentGap, 9.4));
    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageActivitySegmentGap, 3.2));

    EXPECT_EQ(config.layout.driveUsageList.labelGap, 13);
    EXPECT_EQ(config.layout.driveUsageList.barGap, 11);
    EXPECT_EQ(config.layout.driveUsageList.rwGap, 8);
    EXPECT_EQ(config.layout.driveUsageList.percentGap, 9);
    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 3);
}

TEST(LayoutEditCommands, LeavesDriveUsageActivitySegmentGapAsNonNegativeConfigValue) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(config, LayoutEditParameter::DriveUsageActivitySegmentGap, 99.0));

    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 99);
}
