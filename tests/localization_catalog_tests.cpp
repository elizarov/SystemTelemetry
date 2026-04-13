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

    const std::vector<DashboardRenderer::LayoutEditParameter> parameters = {
        DashboardRenderer::LayoutEditParameter::MetricListLabelWidth,
        DashboardRenderer::LayoutEditParameter::MetricListVerticalGap,
        DashboardRenderer::LayoutEditParameter::DriveUsageLabelGap,
        DashboardRenderer::LayoutEditParameter::DriveUsageBarGap,
        DashboardRenderer::LayoutEditParameter::DriveUsageRwGap,
        DashboardRenderer::LayoutEditParameter::DriveUsagePercentGap,
        DashboardRenderer::LayoutEditParameter::DriveUsageActivityWidth,
        DashboardRenderer::LayoutEditParameter::DriveUsageFreeWidth,
        DashboardRenderer::LayoutEditParameter::DriveUsageActivitySegmentGap,
        DashboardRenderer::LayoutEditParameter::DriveUsageHeaderGap,
        DashboardRenderer::LayoutEditParameter::DriveUsageRowGap,
        DashboardRenderer::LayoutEditParameter::ThroughputAxisPadding,
        DashboardRenderer::LayoutEditParameter::ThroughputHeaderGap,
        DashboardRenderer::LayoutEditParameter::ThroughputGuideStrokeWidth,
        DashboardRenderer::LayoutEditParameter::ThroughputPlotStrokeWidth,
        DashboardRenderer::LayoutEditParameter::ThroughputLeaderDiameter,
        DashboardRenderer::LayoutEditParameter::GaugeOuterPadding,
        DashboardRenderer::LayoutEditParameter::GaugeRingThickness,
        DashboardRenderer::LayoutEditParameter::GaugeValueBottom,
        DashboardRenderer::LayoutEditParameter::GaugeLabelBottom,
        DashboardRenderer::LayoutEditParameter::GaugeSweepDegrees,
        DashboardRenderer::LayoutEditParameter::GaugeSegmentGapDegrees,
        DashboardRenderer::LayoutEditParameter::MetricListBarHeight,
        DashboardRenderer::LayoutEditParameter::DriveUsageBarHeight,
        DashboardRenderer::LayoutEditParameter::GaugeSegmentCount,
        DashboardRenderer::LayoutEditParameter::DriveUsageActivitySegments,
        DashboardRenderer::LayoutEditParameter::FontTitle,
        DashboardRenderer::LayoutEditParameter::FontBig,
        DashboardRenderer::LayoutEditParameter::FontValue,
        DashboardRenderer::LayoutEditParameter::FontLabel,
        DashboardRenderer::LayoutEditParameter::FontText,
        DashboardRenderer::LayoutEditParameter::FontSmall,
        DashboardRenderer::LayoutEditParameter::FontFooter,
        DashboardRenderer::LayoutEditParameter::FontClockTime,
        DashboardRenderer::LayoutEditParameter::FontClockDate,
    };

    for (const auto parameter : parameters) {
        const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
        ASSERT_TRUE(descriptor.has_value());
        const auto it = catalog.find(descriptor->configKey);
        ASSERT_TRUE(it != catalog.end()) << "missing localization key: " << descriptor->configKey;
        EXPECT_FALSE(it->second.empty()) << "empty localization text for key: " << descriptor->configKey;
    }
}
