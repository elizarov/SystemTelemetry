#include <gtest/gtest.h>

#include "layout_edit_parameter.h"
#include "layout_edit_tooltip.h"

TEST(LayoutEditTooltip, BuildsMetricListGuideDescriptor) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::MetricListLabelWidth);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.metric_list.label_width");
    EXPECT_EQ(descriptor->sectionName, "metric_list");
    EXPECT_EQ(descriptor->memberName, "label_width");
    EXPECT_EQ(descriptor->valueFormat, configschema::ValueFormat::Integer);
}

TEST(LayoutEditTooltip, BuildsCardStyleDescriptor) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::CardHeaderIconGap);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.card_style.header_icon_gap");
    EXPECT_EQ(descriptor->sectionName, "card_style");
    EXPECT_EQ(descriptor->memberName, "header_icon_gap");
    EXPECT_EQ(descriptor->valueFormat, configschema::ValueFormat::Integer);
}

TEST(LayoutEditTooltip, BuildsDashboardDescriptor) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::DashboardColumnGap);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.dashboard.column_gap");
    EXPECT_EQ(descriptor->sectionName, "dashboard");
    EXPECT_EQ(descriptor->memberName, "column_gap");
    EXPECT_EQ(descriptor->valueFormat, configschema::ValueFormat::Integer);
}

TEST(LayoutEditTooltip, BuildsDashboardOuterMarginDescriptor) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::DashboardOuterMargin);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.dashboard.outer_margin");
    EXPECT_EQ(descriptor->sectionName, "dashboard");
    EXPECT_EQ(descriptor->memberName, "outer_margin");
    EXPECT_EQ(descriptor->valueFormat, configschema::ValueFormat::Integer);
}

TEST(LayoutEditTooltip, IncludesFontAnchors) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::FontLabel);
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->configKey, "config.fonts.label");
    EXPECT_EQ(descriptor->sectionName, "fonts");
    EXPECT_EQ(descriptor->memberName, "label");
    EXPECT_EQ(descriptor->valueFormat, configschema::ValueFormat::FontSpec);
}

TEST(LayoutEditTooltip, FormatsFloatingPointValuesWithoutTrailingZeros) {
    EXPECT_EQ(FormatLayoutEditTooltipValue(2.2, configschema::ValueFormat::FloatingPoint), "2.2");
    EXPECT_EQ(FormatLayoutEditTooltipValue(262.0, configschema::ValueFormat::FloatingPoint), "262");
}

TEST(LayoutEditTooltip, FormatsColorValuesAsUppercaseHex) {
    EXPECT_EQ(FormatLayoutEditTooltipValue(0x12ab34cdu), "#12AB34CD");
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

TEST(LayoutEditTooltip, BuildsColorTooltipFirstLine) {
    const auto descriptor = FindLayoutEditTooltipDescriptor(LayoutEditParameter::ColorAccent);

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(BuildLayoutEditTooltipLine(*descriptor, 0x00BFFFFFu), "[colors] accent_color = #00BFFFFF");
}

TEST(LayoutEditTooltip, BuildsStringTooltipFirstLine) {
    LayoutEditTooltipDescriptor descriptor;
    descriptor.sectionName = "card.cpu";
    descriptor.memberName = "title";
    descriptor.valueFormat = configschema::ValueFormat::String;

    EXPECT_EQ(BuildLayoutEditTooltipLine(descriptor, "CPU"), "[card.cpu] title = CPU");
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

TEST(LayoutEditTooltip, BuildsMetricListOrderTooltipLineForCardLayout) {
    AppConfig config;
    config.display.layout = "main";
    config.layout.cards.push_back(LayoutCardConfig{});
    config.layout.cards.back().id = "cpu";
    config.layout.cards.back().layout.name = "metric_list";
    config.layout.cards.back().layout.parameter = "cpu.ram,cpu.clock,board.temp.cpu";

    const auto line = BuildMetricListOrderTooltipLine(config, LayoutMetricListOrderEditKey{"cpu", {}}, 1);

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "[card.cpu] layout = metric_list(cpu.clock)");
}

TEST(LayoutEditTooltip, BuildsMetricListOrderTooltipLineForDashboardLayout) {
    AppConfig config;
    config.display.layout = "main";
    config.layout.structure.cardsLayout.name = "metric_list";
    config.layout.structure.cardsLayout.parameter = "network.upload,network.download";

    const auto line = BuildMetricListOrderTooltipLine(config, LayoutMetricListOrderEditKey{"", {}}, 0);

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "[layout.main] cards = metric_list(network.upload)");
}

TEST(LayoutEditTooltip, RejectsMetricListOrderTooltipLineWhenRowIndexIsInvalid) {
    AppConfig config;
    config.display.layout = "main";
    config.layout.cards.push_back(LayoutCardConfig{});
    config.layout.cards.back().id = "cpu";
    config.layout.cards.back().layout.name = "metric_list";
    config.layout.cards.back().layout.parameter = "cpu.ram,cpu.clock";

    EXPECT_FALSE(BuildMetricListOrderTooltipLine(config, LayoutMetricListOrderEditKey{"cpu", {}}, -1).has_value());
    EXPECT_FALSE(BuildMetricListOrderTooltipLine(config, LayoutMetricListOrderEditKey{"cpu", {}}, 2).has_value());
}

TEST(LayoutEditTooltip, BuildsMetricListAddRowTooltipLineForCardLayout) {
    AppConfig config;
    config.display.layout = "main";
    config.layout.cards.push_back(LayoutCardConfig{});
    config.layout.cards.back().id = "cpu";
    config.layout.cards.back().layout.name = "metric_list";
    config.layout.cards.back().layout.parameter = "cpu.ram,cpu.clock";

    const auto line = BuildMetricListAddRowTooltipLine(config, LayoutMetricListOrderEditKey{"cpu", {}});

    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "[card.cpu] layout = metric_list(...)");
}

TEST(LayoutEditParameter, UsesReflectedFieldMetadataNames) {
    const auto& gaugeField = GetLayoutEditConfigFieldMetadata(LayoutEditParameter::GaugeSegmentCount);
    EXPECT_EQ(gaugeField.sectionName, "gauge");
    EXPECT_EQ(gaugeField.parameterName, "segment_count");
    EXPECT_EQ(gaugeField.valueFormat, configschema::ValueFormat::Integer);

    const auto& fontField = GetLayoutEditConfigFieldMetadata(LayoutEditParameter::FontLabel);
    EXPECT_EQ(fontField.sectionName, "fonts");
    EXPECT_EQ(fontField.parameterName, "label");
    EXPECT_EQ(fontField.valueFormat, configschema::ValueFormat::FontSpec);

    const auto& colorField = GetLayoutEditConfigFieldMetadata(LayoutEditParameter::ColorAccent);
    EXPECT_EQ(colorField.sectionName, "colors");
    EXPECT_EQ(colorField.parameterName, "accent_color");
    EXPECT_EQ(colorField.valueFormat, configschema::ValueFormat::ColorHex);
}

TEST(LayoutEditParameter, RootLensAppliesIntoNestedConfig) {
    AppConfig config;
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::GaugeSegmentCount, 6.4));
    ASSERT_TRUE(ApplyLayoutEditParameterValue(config, LayoutEditParameter::MetricListBarHeight, 17.2));

    EXPECT_EQ(config.layout.gauge.segmentCount, 6);
    EXPECT_EQ(config.layout.metricList.barHeight, 17);
}

TEST(LayoutEditParameter, RootLensReturnsUnderlyingFontField) {
    AppConfig config;
    config.layout.fonts.label = UiFontConfig{"Segoe UI", 17, 600};

    const auto font = FindLayoutEditTooltipFontValue(config, LayoutEditParameter::FontLabel);

    ASSERT_TRUE(font.has_value());
    EXPECT_EQ(*font, &config.layout.fonts.label);
}

TEST(LayoutEditParameter, AppliesAndReadsBackColorFieldsThroughMetadata) {
    AppConfig config;

    ASSERT_TRUE(ApplyLayoutEditParameterColorValue(config, LayoutEditParameter::ColorAccent, 0x12345678u));
    const auto color = FindLayoutEditParameterColorValue(config, LayoutEditParameter::ColorAccent);

    ASSERT_TRUE(color.has_value());
    EXPECT_EQ(*color, 0x12345678u);
    EXPECT_EQ(config.layout.colors.accentColor.ToRgba(), 0x12345678u);
}

TEST(LayoutEditParameter, BuildsDisplayNamesForMenuActions) {
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::MetricListLabelWidth), "label width");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::GaugeSegmentCount), "segment count");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::FontClockTime), "clock time font");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::CardHeaderContentGap), "header content gap");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::CardRowGap), "row gap");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::DashboardColumnGap), "column gap");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::ColorIcon), "icon color");
    EXPECT_EQ(GetLayoutEditParameterDisplayName(LayoutEditParameter::ColorGraphMarker), "graph marker color");
}

TEST(LayoutEditParameter, AppliesFullFontValueThroughMetadata) {
    AppConfig config;
    config.layout.fonts.label = UiFontConfig{"Segoe UI", 17, 600};

    ASSERT_TRUE(
        ApplyLayoutEditParameterFontValue(config, LayoutEditParameter::FontLabel, UiFontConfig{"Bahnschrift", 0, 450}));

    EXPECT_EQ(config.layout.fonts.label.face, "Bahnschrift");
    EXPECT_EQ(config.layout.fonts.label.size, 1);
    EXPECT_EQ(config.layout.fonts.label.weight, 450);
}

TEST(LayoutEditParameter, ClampsFullFontWeightThroughMetadata) {
    AppConfig config;
    config.layout.fonts.label = UiFontConfig{"Segoe UI", 17, 600};

    ASSERT_TRUE(
        ApplyLayoutEditParameterFontValue(config, LayoutEditParameter::FontLabel, UiFontConfig{"Bahnschrift", 12, 0}));

    EXPECT_EQ(config.layout.fonts.label.face, "Bahnschrift");
    EXPECT_EQ(config.layout.fonts.label.size, 12);
    EXPECT_EQ(config.layout.fonts.label.weight, 1);
}

TEST(LayoutEditParameter, PrioritizesSmallHandlesBeforeGuidesAndRingCircles) {
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::GaugeSegmentCount),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::GaugeOuterPadding));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::FontLabel),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::MetricListLabelWidth));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::ThroughputLeaderDiameter),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::ThroughputAxisPadding));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::CardBorder),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::CardPadding));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::DashboardOuterMargin),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::TextBottomGap));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::CardRowGap),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::TextBottomGap));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::CardColumnGap),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::CardHeaderContentGap));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::CardRowGap),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::CardHeaderContentGap));
    EXPECT_LT(GetLayoutEditParameterHitPriority(LayoutEditParameter::DashboardColumnGap),
        GetLayoutEditParameterHitPriority(LayoutEditParameter::MetricListRowGap));
}

TEST(LayoutEditParameter, MetadataTableMatchesEnumOrder) {
    for (int index = 0; index < static_cast<int>(LayoutEditParameter::Count); ++index) {
        const auto parameter = static_cast<LayoutEditParameter>(index);
        EXPECT_EQ(GetLayoutEditParameterInfo(parameter).parameter, parameter);
    }
}
