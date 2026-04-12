#include <gtest/gtest.h>

#include "layout_edit_tooltip.h"

TEST(LayoutEditTooltip, BuildsMetricListGuideDescriptor) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(DashboardRenderer::WidgetEditParameter::MetricListLabelWidth);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.metric_list.label_width");
    EXPECT_EQ(descriptor->sectionName, "metric_list");
    EXPECT_EQ(descriptor->memberName, "label_width");
    EXPECT_EQ(descriptor->valueFormat, LayoutEditTooltipValueFormat::Integer);
}

TEST(LayoutEditTooltip, IncludesFontAnchors) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(DashboardRenderer::AnchorEditParameter::FontLabel);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.fonts.label");
    EXPECT_EQ(descriptor->sectionName, "fonts");
    EXPECT_EQ(descriptor->memberName, "label");
    EXPECT_EQ(descriptor->valueFormat, LayoutEditTooltipValueFormat::FontSpec);
}

TEST(LayoutEditTooltip, FormatsFloatingPointValuesWithoutTrailingZeros) {
    EXPECT_EQ(FormatLayoutEditTooltipValue(2.2, LayoutEditTooltipValueFormat::FloatingPoint), "2.2");
    EXPECT_EQ(FormatLayoutEditTooltipValue(262.0, LayoutEditTooltipValueFormat::FloatingPoint), "262");
}

TEST(LayoutEditTooltip, BuildsTooltipFirstLine) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(DashboardRenderer::AnchorEditParameter::SegmentCount);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(BuildLayoutEditTooltipLine(*descriptor, 6.0), "[gauge] segment_count = 6");
}

TEST(LayoutEditTooltip, BuildsFontTooltipFirstLine) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(DashboardRenderer::AnchorEditParameter::FontClockTime);

    ASSERT_TRUE(descriptor.has_value());
    const UiFontConfig font{"Segoe UI Semibold", 40, 700};
    EXPECT_EQ(BuildLayoutEditTooltipLine(*descriptor, font), "[fonts] clock_time = Segoe UI Semibold,40,700");
}
