#include <gtest/gtest.h>

#include "layout_edit_parameter.h"
#include "layout_edit_tooltip.h"

TEST(LayoutEditTooltip, BuildsMetricListGuideDescriptor) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::MetricListLabelWidth);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.metric_list.label_width");
    EXPECT_EQ(descriptor->sectionName, "metric_list");
    EXPECT_EQ(descriptor->memberName, "label_width");
    EXPECT_EQ(descriptor->valueFormat, LayoutEditTooltipValueFormat::Integer);
}

TEST(LayoutEditTooltip, IncludesFontAnchors) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::FontLabel);
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
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::GaugeSegmentCount);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(BuildLayoutEditTooltipLine(*descriptor, 6.0), "[gauge] segment_count = 6");
}

TEST(LayoutEditTooltip, BuildsFontTooltipFirstLine) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::FontClockTime);

    ASSERT_TRUE(descriptor.has_value());
    const UiFontConfig font{"Segoe UI Semibold", 40, 700};
    EXPECT_EQ(BuildLayoutEditTooltipLine(*descriptor, font), "[fonts] clock_time = Segoe UI Semibold,40,700");
}

TEST(LayoutEditTooltip, ResolvesFontValueThroughParameterMetadata) {
    AppConfig config;
    config.layout.fonts.label = UiFontConfig{"Segoe UI", 17, 600};

    const auto font = FindLayoutEditTooltipFontValue(config, LayoutEditParameter::FontLabel);

    ASSERT_TRUE(font.has_value());
    ASSERT_NE(*font, nullptr);
    EXPECT_EQ((*font)->face, "Segoe UI");
    EXPECT_EQ((*font)->size, 17);
    EXPECT_EQ((*font)->weight, 600);
}

TEST(LayoutEditParameter, PrioritizesSmallHandlesBeforeGuidesAndRingCircles) {
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::GaugeSegmentCount),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::GaugeOuterPadding));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::FontLabel),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::MetricListLabelWidth));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::ThroughputLeaderDiameter),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::ThroughputAxisPadding));
}

TEST(LayoutEditParameter, MetadataTableMatchesEnumOrder) {
    for (int index = 0; index < static_cast<int>(LayoutEditParameter::Count); ++index) {
        const auto parameter = static_cast<LayoutEditParameter>(index);
        EXPECT_EQ(GetLayoutEditParameterInfo(parameter).parameter, parameter);
    }
}
