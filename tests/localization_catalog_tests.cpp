#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "layout_edit_tooltip.h"
#include "localization_catalog.h"

TEST(LocalizationCatalog, ParsesKeyValueLines) {
    const LocalizationCatalogMap catalog = ParseLocalizationCatalog(
        "# comment\n"
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

    const std::vector<DashboardRenderer::WidgetEditParameter> widgetParameters = {
        DashboardRenderer::WidgetEditParameter::MetricListLabelWidth,
        DashboardRenderer::WidgetEditParameter::MetricListVerticalGap,
        DashboardRenderer::WidgetEditParameter::DriveUsageLabelGap,
        DashboardRenderer::WidgetEditParameter::DriveUsageBarGap,
        DashboardRenderer::WidgetEditParameter::DriveUsageRwGap,
        DashboardRenderer::WidgetEditParameter::DriveUsagePercentGap,
        DashboardRenderer::WidgetEditParameter::DriveUsageActivityWidth,
        DashboardRenderer::WidgetEditParameter::DriveUsageFreeWidth,
        DashboardRenderer::WidgetEditParameter::DriveUsageActivitySegmentGap,
        DashboardRenderer::WidgetEditParameter::DriveUsageHeaderGap,
        DashboardRenderer::WidgetEditParameter::DriveUsageRowGap,
        DashboardRenderer::WidgetEditParameter::ThroughputAxisPadding,
        DashboardRenderer::WidgetEditParameter::ThroughputHeaderGap,
        DashboardRenderer::WidgetEditParameter::ThroughputGuideStrokeWidth,
        DashboardRenderer::WidgetEditParameter::ThroughputPlotStrokeWidth,
        DashboardRenderer::WidgetEditParameter::ThroughputLeaderDiameter,
        DashboardRenderer::WidgetEditParameter::GaugeOuterPadding,
        DashboardRenderer::WidgetEditParameter::GaugeRingThickness,
        DashboardRenderer::WidgetEditParameter::GaugeValueBottom,
        DashboardRenderer::WidgetEditParameter::GaugeLabelBottom,
        DashboardRenderer::WidgetEditParameter::GaugeSweepDegrees,
        DashboardRenderer::WidgetEditParameter::GaugeSegmentGapDegrees,
    };
    const std::vector<DashboardRenderer::AnchorEditParameter> anchorParameters = {
        DashboardRenderer::AnchorEditParameter::MetricListBarHeight,
        DashboardRenderer::AnchorEditParameter::DriveUsageBarHeight,
        DashboardRenderer::AnchorEditParameter::SegmentCount,
        DashboardRenderer::AnchorEditParameter::DriveUsageActivitySegments,
        DashboardRenderer::AnchorEditParameter::ThroughputGuideStrokeWidth,
        DashboardRenderer::AnchorEditParameter::ThroughputPlotStrokeWidth,
        DashboardRenderer::AnchorEditParameter::ThroughputLeaderDiameter,
        DashboardRenderer::AnchorEditParameter::GaugeOuterPadding,
        DashboardRenderer::AnchorEditParameter::GaugeRingThickness,
        DashboardRenderer::AnchorEditParameter::FontTitle,
        DashboardRenderer::AnchorEditParameter::FontBig,
        DashboardRenderer::AnchorEditParameter::FontValue,
        DashboardRenderer::AnchorEditParameter::FontLabel,
        DashboardRenderer::AnchorEditParameter::FontText,
        DashboardRenderer::AnchorEditParameter::FontSmall,
        DashboardRenderer::AnchorEditParameter::FontFooter,
        DashboardRenderer::AnchorEditParameter::FontClockTime,
        DashboardRenderer::AnchorEditParameter::FontClockDate,
    };

    for (const auto parameter : widgetParameters) {
        const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
        ASSERT_TRUE(descriptor.has_value());
        const auto it = catalog.find(descriptor->configKey);
        ASSERT_TRUE(it != catalog.end()) << "missing localization key: " << descriptor->configKey;
        EXPECT_FALSE(it->second.empty()) << "empty localization text for key: " << descriptor->configKey;
    }

    for (const auto parameter : anchorParameters) {
        const auto descriptor = FindLayoutEditTooltipDescriptor(parameter);
        ASSERT_TRUE(descriptor.has_value());
        const auto it = catalog.find(descriptor->configKey);
        ASSERT_TRUE(it != catalog.end()) << "missing localization key: " << descriptor->configKey;
        EXPECT_FALSE(it->second.empty()) << "empty localization text for key: " << descriptor->configKey;
    }
}
