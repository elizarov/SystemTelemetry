#include <gtest/gtest.h>

#include "layout_edit_commands.h"

namespace {

LayoutEditHost::ValueTarget MakeTarget(LayoutEditHost::ValueTarget::Field field) {
    LayoutEditHost::ValueTarget target;
    target.field = field;
    return target;
}

}  // namespace

TEST(LayoutEditCommands, ClampsGaugeSegmentGapAgainstSweepAndSegmentCount) {
    AppConfig config;
    config.layout.gauge.sweepDegrees = 180.0;
    config.layout.gauge.segmentCount = 4;

    ASSERT_TRUE(layout_edit::ApplyValue(
        config,
        MakeTarget(LayoutEditHost::ValueTarget::Field::GaugeSegmentGapDegrees),
        100.0));

    EXPECT_EQ(config.layout.gauge.segmentGapDegrees, 60.0);
}

TEST(LayoutEditCommands, ClampsFontSizesToPositiveValues) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(
        config,
        MakeTarget(LayoutEditHost::ValueTarget::Field::FontFooter),
        0.2));

    EXPECT_EQ(config.layout.fonts.footer.size, 1);
}

TEST(LayoutEditCommands, UpdatesMetricListAndGaugeFieldsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(
        config,
        MakeTarget(LayoutEditHost::ValueTarget::Field::MetricListBarHeight),
        17.2));
    ASSERT_TRUE(layout_edit::ApplyValue(
        config,
        MakeTarget(LayoutEditHost::ValueTarget::Field::GaugeSegmentCount),
        6.4));

    EXPECT_EQ(config.layout.metricList.barHeight, 17);
    EXPECT_EQ(config.layout.gauge.segmentCount, 6);
}

TEST(LayoutEditCommands, UpdatesDriveUsageActivitySegmentsThroughCommands) {
    AppConfig config;

    ASSERT_TRUE(layout_edit::ApplyValue(
        config,
        MakeTarget(LayoutEditHost::ValueTarget::Field::DriveUsageActivitySegments),
        5.6));

    EXPECT_EQ(config.layout.driveUsageList.activitySegments, 6);
}
