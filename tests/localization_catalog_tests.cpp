#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "layout_edit_tooltip.h"
#include "localization_catalog.h"

TEST(LocalizationCatalog, ParsesKeyValueLines) {
    const LocalizationCatalogMap catalog =
        ParseLocalizationCatalog("# comment\n"
                                 "config.metric_list.label_width = Label width text\n"
                                 "\n"
                                 "config.gauge.segment_count= Segment count text\n");

    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog.at("config.metric_list.label_width"), "Label width text");
    EXPECT_EQ(catalog.at("config.gauge.segment_count"), "Segment count text");
}

TEST(LocalizationCatalog, DefinesTextForAllSupportedTooltipKeys) {
    const std::filesystem::path catalogPath =
        std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "localization.ini";
    std::ifstream input(catalogPath, std::ios::binary);
    ASSERT_TRUE(input.is_open()) << "failed to open " << catalogPath.string();

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const LocalizationCatalogMap catalog = ParseLocalizationCatalog(buffer.str());

    const std::vector<LayoutEditParameter> parameters = {
        LayoutEditParameter::MetricListLabelWidth,
        LayoutEditParameter::MetricListRowGap,
        LayoutEditParameter::DriveUsageLabelGap,
        LayoutEditParameter::DriveUsageBarGap,
        LayoutEditParameter::DriveUsageRwGap,
        LayoutEditParameter::DriveUsagePercentGap,
        LayoutEditParameter::DriveUsageActivityWidth,
        LayoutEditParameter::DriveUsageFreeWidth,
        LayoutEditParameter::DriveUsageActivitySegmentGap,
        LayoutEditParameter::DriveUsageHeaderGap,
        LayoutEditParameter::DriveUsageRowGap,
        LayoutEditParameter::ThroughputAxisPadding,
        LayoutEditParameter::ThroughputHeaderGap,
        LayoutEditParameter::ThroughputGuideStrokeWidth,
        LayoutEditParameter::ThroughputPlotStrokeWidth,
        LayoutEditParameter::ThroughputLeaderDiameter,
        LayoutEditParameter::GaugeOuterPadding,
        LayoutEditParameter::GaugeRingThickness,
        LayoutEditParameter::GaugeValueBottom,
        LayoutEditParameter::GaugeLabelBottom,
        LayoutEditParameter::GaugeSweepDegrees,
        LayoutEditParameter::GaugeSegmentGapDegrees,
        LayoutEditParameter::MetricListBarHeight,
        LayoutEditParameter::DriveUsageBarHeight,
        LayoutEditParameter::GaugeSegmentCount,
        LayoutEditParameter::DriveUsageActivitySegments,
        LayoutEditParameter::FontTitle,
        LayoutEditParameter::FontBig,
        LayoutEditParameter::FontValue,
        LayoutEditParameter::FontLabel,
        LayoutEditParameter::FontText,
        LayoutEditParameter::FontSmall,
        LayoutEditParameter::FontFooter,
        LayoutEditParameter::FontClockTime,
        LayoutEditParameter::FontClockDate,
        LayoutEditParameter::CardPadding,
        LayoutEditParameter::CardRadius,
        LayoutEditParameter::CardBorder,
        LayoutEditParameter::CardHeaderIconSize,
        LayoutEditParameter::CardHeaderIconGap,
        LayoutEditParameter::CardHeaderContentGap,
        LayoutEditParameter::CardRowGap,
        LayoutEditParameter::CardColumnGap,
        LayoutEditParameter::DashboardRowGap,
        LayoutEditParameter::DashboardColumnGap,
    };

    for (const auto parameter : parameters) {
        const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
        ASSERT_TRUE(descriptor.has_value());
        const auto it = catalog.find(descriptor->configKey);
        ASSERT_TRUE(it != catalog.end()) << "missing localization key: " << descriptor->configKey;
        EXPECT_FALSE(it->second.empty()) << "empty localization text for key: " << descriptor->configKey;
    }
}
