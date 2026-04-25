#include <gtest/gtest.h>

#include "layout_edit/layout_edit_parameter_edit.h"

TEST(LayoutEditParameterApply, ClampsGaugeDegreeFieldsToZeroTo360) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeSegmentGapDegrees, 100.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeSweepDegrees, 500.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeSegmentGapDegrees, -1.0));

    EXPECT_EQ(config.layout.gauge.sweepDegrees, 360.0);
    EXPECT_EQ(config.layout.gauge.segmentGapDegrees, 0.0);
}

TEST(LayoutEditParameterApply, ClampsFontSizesToPositiveValues) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::FontFooter, 0.2));

    EXPECT_EQ(config.layout.fonts.footer.size, 1);
}

TEST(LayoutEditParameterApply, AllowsZeroForEditablePaddingAndGapFields) {
    AppConfig config;
    config.layout.fonts.label.size = 15;
    config.layout.fonts.smallText.size = 13;
    config.layout.driveUsageList.barHeight = 10;
    config.layout.driveUsageList.activitySegments = 4;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::TextBottomGap, -3.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::NetworkFooterBottomGap, -3.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::MetricListRowGap, -2.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageLabelGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageRwGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageBarGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsagePercentGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageHeaderGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageRowGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageActivitySegmentGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::ThroughputAxisPadding, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::ThroughputHeaderGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeOuterPadding, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardPadding, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardHeaderIconGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardHeaderContentGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardRowGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardColumnGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DashboardOuterMargin, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DashboardRowGap, -1.0));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DashboardColumnGap, -1.0));

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

TEST(LayoutEditParameterApply, UpdatesMetricListAndGaugeFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::MetricListBarHeight, 17.2));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeSegmentCount, 6.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeOuterPadding, 9.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeRingThickness, 11.2));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeValueBottom, 17.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeLabelBottom, 33.2));

    EXPECT_EQ(config.layout.metricList.barHeight, 17);
    EXPECT_EQ(config.layout.gauge.segmentCount, 6);
    EXPECT_EQ(config.layout.gauge.outerPadding, 10);
    EXPECT_EQ(config.layout.gauge.ringThickness, 11);
    EXPECT_EQ(config.layout.gauge.valueBottom, 18);
    EXPECT_EQ(config.layout.gauge.labelBottom, 33);
}

TEST(LayoutEditParameterApply, UpdatesDriveUsageActivitySegmentsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageActivitySegments, 5.6));

    EXPECT_EQ(config.layout.driveUsageList.activitySegments, 6);
}

TEST(LayoutEditParameterApply, UpdatesThroughputAnchorFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::ThroughputGuideStrokeWidth, 3.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::ThroughputPlotStrokeWidth, 5.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::ThroughputLeaderDiameter, 8.6));

    EXPECT_EQ(config.layout.throughput.guideStrokeWidth, 4);
    EXPECT_EQ(config.layout.throughput.plotStrokeWidth, 6);
    EXPECT_EQ(config.layout.throughput.leaderDiameter, 9);
}

TEST(LayoutEditParameterApply, UpdatesCardStyleFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardRadius, 13.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardBorder, 2.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardHeaderIconSize, 19.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardPadding, 12.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardHeaderIconGap, 7.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardHeaderContentGap, 5.4));

    EXPECT_EQ(config.layout.cardStyle.cardRadius, 14);
    EXPECT_EQ(config.layout.cardStyle.cardBorderWidth, 3);
    EXPECT_EQ(config.layout.cardStyle.headerIconSize, 20);
    EXPECT_EQ(config.layout.cardStyle.cardPadding, 12);
    EXPECT_EQ(config.layout.cardStyle.headerIconGap, 7);
    EXPECT_EQ(config.layout.cardStyle.headerContentGap, 5);
}

TEST(LayoutEditParameterApply, UpdatesSharedContainerGapFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardRowGap, 3.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::CardColumnGap, 8.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DashboardOuterMargin, 5.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DashboardRowGap, 9.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DashboardColumnGap, 11.6));

    EXPECT_EQ(config.layout.cardStyle.rowGap, 4);
    EXPECT_EQ(config.layout.cardStyle.columnGap, 9);
    EXPECT_EQ(config.layout.dashboard.outerMargin, 6);
    EXPECT_EQ(config.layout.dashboard.rowGap, 10);
    EXPECT_EQ(config.layout.dashboard.columnGap, 12);
}

TEST(LayoutEditParameterApply, UpdatesDriveUsageGapFieldsThroughCommands) {
    AppConfig config;
    config.layout.fonts.label.size = 15;
    config.layout.fonts.smallText.size = 13;
    config.layout.driveUsageList.barHeight = 10;
    config.layout.driveUsageList.activitySegments = 4;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageLabelGap, 13.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageBarGap, 11.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageRwGap, 7.6));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsagePercentGap, 9.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageActivitySegmentGap, 3.2));

    EXPECT_EQ(config.layout.driveUsageList.labelGap, 13);
    EXPECT_EQ(config.layout.driveUsageList.barGap, 11);
    EXPECT_EQ(config.layout.driveUsageList.rwGap, 8);
    EXPECT_EQ(config.layout.driveUsageList.percentGap, 9);
    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 3);
}

TEST(LayoutEditParameterApply, LeavesDriveUsageActivitySegmentGapAsNonNegativeConfigValue) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::DriveUsageActivitySegmentGap, 99.0));

    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 99);
}

TEST(LayoutEditParameterApply, UpdatesColorFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterColorValue(config, LayoutEditParameter::ColorForeground, 0xABCDEF12u));
    ASSERT_TRUE(ApplyLayoutEditParameterColorValue(config, LayoutEditParameter::ColorIcon, 0xFEDCBA34u));
    ASSERT_TRUE(ApplyLayoutEditParameterColorValue(config, LayoutEditParameter::ColorTrack, 0x10203056u));

    EXPECT_EQ(config.layout.colors.foregroundColor.ToRgba(), 0xABCDEF12u);
    EXPECT_EQ(config.layout.colors.iconColor.ToRgba(), 0xFEDCBA34u);
    EXPECT_EQ(config.layout.colors.trackColor.ToRgba(), 0x10203056u);
}

TEST(LayoutEditParameterApply, AppliesColorFieldsViaMetadata) {
    struct TestCase {
        LayoutEditParameter parameter;
        unsigned int value;
    };

    const TestCase cases[] = {
        {LayoutEditParameter::ColorBackground, 0x11223310u},
        {LayoutEditParameter::ColorForeground, 0x44556620u},
        {LayoutEditParameter::ColorIcon, 0x55667730u},
        {LayoutEditParameter::ColorAccent, 0x77889940u},
        {LayoutEditParameter::ColorPeakGhost, 0x8899AA50u},
        {LayoutEditParameter::ColorLayoutGuide, 0xAABBCC60u},
        {LayoutEditParameter::ColorActiveEdit, 0xDDEEFF70u},
        {LayoutEditParameter::ColorPanelBorder, 0x12345680u},
        {LayoutEditParameter::ColorMutedText, 0x23456790u},
        {LayoutEditParameter::ColorTrack, 0x345678A0u},
        {LayoutEditParameter::ColorPanelFill, 0x456789B0u},
        {LayoutEditParameter::ColorGraphBackground, 0x56789AC0u},
        {LayoutEditParameter::ColorGraphAxis, 0x6789ABD0u},
        {LayoutEditParameter::ColorGraphMarker, 0x789ABCE0u},
    };

    AppConfig config;
    for (const auto& testCase : cases) {
        const auto& field = GetLayoutEditConfigFieldMetadata(testCase.parameter);
        ASSERT_EQ(field.valueFormat, configschema::ValueFormat::ColorHex);
        ASSERT_NE(field.applyColorValue, nullptr);
        ASSERT_NE(field.colorValue, nullptr);

        ASSERT_TRUE(field.applyColorValue(config, testCase.value));
        ASSERT_TRUE(field.colorValue(config).has_value());
        EXPECT_EQ(*field.colorValue(config), testCase.value);
        ASSERT_TRUE(FindLayoutEditParameterColorValue(config, testCase.parameter).has_value());
        EXPECT_EQ(*FindLayoutEditParameterColorValue(config, testCase.parameter), testCase.value);
    }
}
