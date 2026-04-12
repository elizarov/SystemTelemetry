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

TEST(LayoutEditTooltip, OmitsFontAnchors) {
    EXPECT_FALSE(FindLayoutEditTooltipDescriptor(DashboardRenderer::AnchorEditParameter::FontLabel).has_value());
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
