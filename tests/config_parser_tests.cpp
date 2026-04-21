#include <gtest/gtest.h>

#include "config/config_parser.h"
#include "util/utf8.h"
#include <fstream>

namespace {

std::filesystem::path WriteTestConfig(const std::string& text) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "systemtelemetry_config_parser_test.ini";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return path;
}

}  // namespace

TEST(ConfigParser, ClampsParsedEditableValuesThroughSchemaPolicies) {
    const std::filesystem::path path = WriteTestConfig("[fonts]\n"
                                                       "label = Segoe UI,-5,600\n"
                                                       "\n"
                                                       "[drive_usage_list]\n"
                                                       "activity_segment_gap = -9\n"
                                                       "\n"
                                                       "[gauge]\n"
                                                       "sweep_degrees = 500\n"
                                                       "segment_gap_degrees = -12\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(config.layout.fonts.label.size, 1);
    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 0);
    EXPECT_EQ(config.layout.gauge.sweepDegrees, 360.0);
    EXPECT_EQ(config.layout.gauge.segmentGapDegrees, 0.0);

    std::filesystem::remove(path);
}

TEST(ConfigParser, ParsesRenamedCardStyleKeys) {
    const std::filesystem::path path = WriteTestConfig("[card_style]\n"
                                                       "header_icon_size = 21\n"
                                                       "header_icon_gap = 7\n"
                                                       "header_content_gap = 5\n"
                                                       "row_gap = 4\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(config.layout.cardStyle.headerIconSize, 21);
    EXPECT_EQ(config.layout.cardStyle.headerIconGap, 7);
    EXPECT_EQ(config.layout.cardStyle.headerContentGap, 5);
    EXPECT_EQ(config.layout.cardStyle.rowGap, 4);

    std::filesystem::remove(path);
}

TEST(ConfigParser, ParsesRenamedDashboardColumnGapKey) {
    const std::filesystem::path path = WriteTestConfig("[dashboard]\n"
                                                       "outer_margin = 8\n"
                                                       "row_gap = 9\n"
                                                       "column_gap = 11\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(config.layout.dashboard.outerMargin, 8);
    EXPECT_EQ(config.layout.dashboard.rowGap, 9);
    EXPECT_EQ(config.layout.dashboard.columnGap, 11);

    std::filesystem::remove(path);
}

TEST(ConfigParser, ParsesEightDigitColorAlphaAndRejectsSixDigitColors) {
    const std::filesystem::path path = WriteTestConfig("[colors]\n"
                                                       "accent_color = #12345678\n"
                                                       "track_color = #ABCDEF\n");

    const AppConfig config = LoadConfig(path, true);
    const AppConfig defaults;

    EXPECT_EQ(config.layout.colors.accentColor.ToRgba(), 0x12345678u);
    EXPECT_EQ(config.layout.colors.trackColor, defaults.layout.colors.trackColor);

    std::filesystem::remove(path);
}

TEST(ConfigParser, ParsesMetricsSectionEntries) {
    const std::filesystem::path path = WriteTestConfig("[metrics]\n"
                                                       "cpu.load = *,%,Processor Load\n"
                                                       "gpu.temp = 110,C,GPU Temp\n");

    const AppConfig config = LoadConfig(path, true);

    const MetricDefinitionConfig* loadMetric = FindMetricDefinition(config.layout.metrics, "cpu.load");
    ASSERT_NE(loadMetric, nullptr);
    EXPECT_EQ(loadMetric->style, MetricDisplayStyle::Percent);
    EXPECT_TRUE(loadMetric->telemetryScale);
    EXPECT_EQ(loadMetric->unit, "%");
    EXPECT_EQ(loadMetric->label, "Processor Load");

    const MetricDefinitionConfig* gpuTemp = FindMetricDefinition(config.layout.metrics, "gpu.temp");
    ASSERT_NE(gpuTemp, nullptr);
    EXPECT_EQ(gpuTemp->style, MetricDisplayStyle::Scalar);
    EXPECT_FALSE(gpuTemp->telemetryScale);
    EXPECT_DOUBLE_EQ(gpuTemp->scale, 110.0);
    EXPECT_EQ(gpuTemp->unit, "C");
    EXPECT_EQ(gpuTemp->label, "GPU Temp");

    std::filesystem::remove(path);
}

TEST(ConfigParser, IgnoresRuntimePlaceholderMetricMetadataInMetricsSection) {
    const std::filesystem::path path = WriteTestConfig("[metrics]\n"
                                                       "nothing = 7,ignored,Overridden Placeholder\n"
                                                       "cpu.load = *,%,Processor Load\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(FindMetricDefinition(config.layout.metrics, "nothing"), nullptr);
    const MetricDefinitionConfig* loadMetric = FindMetricDefinition(config.layout.metrics, "cpu.load");
    ASSERT_NE(loadMetric, nullptr);
    EXPECT_EQ(loadMetric->label, "Processor Load");

    std::filesystem::remove(path);
}

TEST(ConfigParser, ParsesNamedLayoutSectionsThroughReflectedDynamicBindings) {
    const std::filesystem::path path = WriteTestConfig("[display]\n"
                                                       "layout = portrait\n"
                                                       "\n"
                                                       "[layout.portrait]\n"
                                                       "description = Portrait Mode\n"
                                                       "window = 480,800\n"
                                                       "cards = columns(cpu,gpu)\n");

    const AppConfig config = LoadConfig(path, true);

    ASSERT_EQ(config.layout.layouts.size(), 1u);
    EXPECT_EQ(config.layout.layouts[0].name, "portrait");
    EXPECT_EQ(config.layout.layouts[0].description, "Portrait Mode");
    EXPECT_EQ(config.layout.layouts[0].window.width, 480);
    EXPECT_EQ(config.layout.layouts[0].window.height, 800);
    EXPECT_EQ(config.layout.layouts[0].cardsLayout.name, "columns");
    ASSERT_EQ(config.layout.layouts[0].cardsLayout.children.size(), 2u);
    EXPECT_EQ(config.layout.layouts[0].cardsLayout.children[0].name, "cpu");
    EXPECT_EQ(config.layout.layouts[0].cardsLayout.children[1].name, "gpu");

    std::filesystem::remove(path);
}

TEST(ConfigParser, UsesMetadataOwnedMetricStyleInsteadOfSerializedStyleToken) {
    const std::filesystem::path path = WriteTestConfig("[metrics]\n"
                                                       "cpu.load = *,%,Processor Load\n");

    const AppConfig config = LoadConfig(path, true);

    const MetricDefinitionConfig* loadMetric = FindMetricDefinition(config.layout.metrics, "cpu.load");
    ASSERT_NE(loadMetric, nullptr);
    EXPECT_EQ(loadMetric->style, MetricDisplayStyle::Percent);
    EXPECT_TRUE(loadMetric->telemetryScale);
    EXPECT_EQ(loadMetric->unit, "%");
    EXPECT_EQ(loadMetric->label, "Processor Load");

    std::filesystem::remove(path);
}

TEST(ConfigParser, RejectsSerializedMetricStyleTokensInMetricsSection) {
    const std::filesystem::path path = WriteTestConfig("[metrics]\n"
                                                       "cpu.load = percent,*,%,Processor Load\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(FindMetricDefinition(config.layout.metrics, "cpu.load"), nullptr);

    std::filesystem::remove(path);
}

TEST(ConfigParser, RejectsUnknownMetricIdsWithoutMetadataStyle) {
    const std::filesystem::path path = WriteTestConfig("[metrics]\n"
                                                       "custom.metric = 100,U,Custom\n");

    const AppConfig config = LoadConfig(path, true);

    EXPECT_EQ(FindMetricDefinition(config.layout.metrics, "custom.metric"), nullptr);

    std::filesystem::remove(path);
}

TEST(ConfigParser, CheckedInConfigTemplateUsesValidUtf8) {
    const std::filesystem::path path = std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.is_open()) << "failed to open " << path.string();

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    EXPECT_TRUE(IsValidUtf8(text));
}
