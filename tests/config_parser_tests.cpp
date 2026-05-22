#include <fstream>
#include <gtest/gtest.h>

#include "config/color_expression.h"
#include "config/color_math.h"
#include "config/config_parser.h"
#include "config/config_telemetry.h"
#include "telemetry/metrics.h"

namespace {

FilePath WriteTestConfig(const std::string& text) {
    const FilePath path = TempDirectoryPath() / "casedash_config_parser_test.ini";
    std::ofstream output(path.string(), std::ios::binary | std::ios::trunc);
    output << text;
    return path;
}

ConfigParseContext TestConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

}  // namespace

TEST(ConfigParser, ClampsParsedEditableValuesThroughSchemaPolicies) {
    const FilePath path = WriteTestConfig(
        "[fonts]\n"
        "label = Segoe UI,-5,600\n"
        "\n"
        "[drive_usage_list]\n"
        "activity_segment_gap = -9\n"
        "\n"
        "[gauge]\n"
        "sweep_degrees = 500\n"
        "segment_gap_degrees = -12\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.layout.fonts.label.size, 1);
    EXPECT_EQ(config.layout.driveUsageList.activitySegmentGap, 0);
    EXPECT_EQ(config.layout.gauge.sweepDegrees, 360.0);
    EXPECT_EQ(config.layout.gauge.segmentGapDegrees, 0.0);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesRenamedCardStyleKeys) {
    const FilePath path = WriteTestConfig(
        "[card_style]\n"
        "header_icon_size = 21\n"
        "header_icon_gap = 7\n"
        "header_content_gap = 5\n"
        "row_gap = 4\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.layout.cardStyle.headerIconSize, 21);
    EXPECT_EQ(config.layout.cardStyle.headerIconGap, 7);
    EXPECT_EQ(config.layout.cardStyle.headerContentGap, 5);
    EXPECT_EQ(config.layout.cardStyle.rowGap, 4);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesRenamedDashboardColumnGapKey) {
    const FilePath path = WriteTestConfig(
        "[dashboard]\n"
        "outer_margin = 8\n"
        "row_gap = 9\n"
        "column_gap = 11\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.layout.dashboard.outerMargin, 8);
    EXPECT_EQ(config.layout.dashboard.rowGap, 9);
    EXPECT_EQ(config.layout.dashboard.columnGap, 11);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesGpuAdapterSelection) {
    const FilePath path = WriteTestConfig(
        "[gpu]\n"
        "adapter_name = NVIDIA GeForce RTX 4070 Laptop GPU\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.gpu.adapterName, "NVIDIA GeForce RTX 4070 Laptop GPU");

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesEightDigitColorAlphaAndRejectsSixDigitColors) {
    const FilePath path = WriteTestConfig(
        "[colors]\n"
        "accent_color = #12345678\n"
        "track_color = #ABCDEF\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());
    const AppConfig defaults;

    EXPECT_EQ(config.layout.colors.accentColor.ToRgba(), 0x12345678u);
    EXPECT_EQ(config.layout.colors.trackColor.ToRgba(), defaults.layout.colors.trackColor.ToRgba());

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ResolvesThemeTokensAndDerivedColors) {
    const FilePath path = WriteTestConfig(
        "[display]\n"
        "theme = dark_cyan\n"
        "\n"
        "[theme.dark_cyan]\n"
        "background = #000000FF\n"
        "foreground = #FFFFFFFF\n"
        "accent = #00BFFFFF\n"
        "guide = #FF6A00FF\n"
        "\n"
        "[colors]\n"
        "accent_color = accent\n"
        "peak_ghost_color = accent(alpha: 0x60)\n"
        "active_edit_color = guide(rotate_hue: 46, mix: 0.22 foreground)\n"
        "panel_border_color = background(mix: 0.34 accent)\n"
        "muted_text_color = foreground(mix: 0.55 accent)\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.layout.colors.accentColor.ToRgba(), 0x00BFFFFFu);
    EXPECT_EQ(config.layout.colors.peakGhostColor.ToRgba(), 0x00BFFF60u);
    EXPECT_EQ(config.layout.colors.activeEditColor.ToRgba(), 0xD9AD5AFFu);
    EXPECT_EQ(config.layout.colors.panelBorderColor.ToRgba(), 0x002738FFu);
    EXPECT_EQ(config.layout.colors.mutedTextColor.ToRgba(), 0x99DDFFFFu);

    RemoveFileIfExists(path);
}

TEST(ColorExpression, ParsesAndFormatsDerivedExpressionsInCanonicalOptionOrder) {
    const std::optional<ColorExpression> expression =
        ParseColorExpression("guide(alpha: 230, mix: 0.35 active_edit_color, rotate_hue: 28)");

    ASSERT_TRUE(expression.has_value());
    const ColorExpression& parsedExpression = *expression;
    EXPECT_EQ(parsedExpression.base, "guide");
    ASSERT_TRUE(parsedExpression.rotateHue.has_value());
    const double rotateHue = *parsedExpression.rotateHue;
    EXPECT_DOUBLE_EQ(rotateHue, 28.0);
    ASSERT_TRUE(parsedExpression.mix.has_value());
    const ColorMixExpression& mixExpression = *parsedExpression.mix;
    EXPECT_EQ(mixExpression.target, "active_edit_color");
    EXPECT_DOUBLE_EQ(mixExpression.amount, 0.35);
    ASSERT_TRUE(parsedExpression.alpha.has_value());
    const uint32_t alpha = *parsedExpression.alpha;
    EXPECT_EQ(alpha, 230u);
    EXPECT_EQ(
        FormatColorExpression(parsedExpression), "guide(rotate_hue: 28, mix: 0.35 active_edit_color, alpha: 0xE6)");
}

TEST(ColorMath, ConvertsHsvAndRgbRoundTrip) {
    const HsvColor cyan = HsvFromColorBytes(ColorBytesFromRgba(0x00BFFFFFu));

    EXPECT_NEAR(cyan.h, 195.0, 0.1);
    EXPECT_NEAR(cyan.s, 1.0, 0.001);
    EXPECT_NEAR(cyan.v, 1.0, 0.001);

    EXPECT_EQ(RgbaFromColorBytes(ColorBytesFromHsv(cyan, 255.0)), 0x00BFFFFFu);
    EXPECT_EQ(RgbaFromColorBytes(ColorBytesFromHsv(HsvColor{0.0, 0.0, 0.5}, 255.0)), 0x808080FFu);
}

TEST(ConfigParser, ResolvesLayoutGuideSheetColorsFromThemeAndColorsSection) {
    const FilePath path = WriteTestConfig(
        "[display]\n"
        "theme = dark_cyan\n"
        "\n"
        "[theme.dark_cyan]\n"
        "background = #000000FF\n"
        "foreground = #FFFFFFFF\n"
        "accent = #00BFFFFF\n"
        "guide = #FF6A00FF\n"
        "\n"
        "[colors]\n"
        "active_edit_color = guide(rotate_hue: 46, mix: 0.22 foreground)\n"
        "muted_text_color = foreground(mix: 0.55 accent)\n"
        "\n"
        "[layout_guide_sheet]\n"
        "callout_leader_color = foreground(mix: 0.59 guide, alpha: 0xE6)\n"
        "callout_border_color = guide(rotate_hue: 53)\n"
        "callout_description_color = muted_text_color\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.layout.layoutGuideSheet.calloutLeaderColor.ToRgba(), 0xFFAC84E6u);
    EXPECT_EQ(config.layout.layoutGuideSheet.calloutBorderColor.ToRgba(), 0xC19C00FFu);
    EXPECT_EQ(config.layout.layoutGuideSheet.calloutDescriptionColor.ToRgba(), 0x99DDFFFFu);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesLayoutGuideSheetSection) {
    const FilePath path = WriteTestConfig(
        "[layout_guide_sheet]\n"
        "callout_leader_color = #FFE45CE6\n"
        "callout_border_color = #B88A22FF\n"
        "sheet_margin = 41\n"
        "callout_gap = 42\n"
        "leader_stroke_width = 3\n"
        "leader_endpoint_diameter = 7\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(config.layout.layoutGuideSheet.calloutLeaderColor.ToRgba(), 0xFFE45CE6u);
    EXPECT_EQ(config.layout.layoutGuideSheet.calloutBorderColor.ToRgba(), 0xB88A22FFu);
    EXPECT_EQ(config.layout.layoutGuideSheet.sheetMargin, 41);
    EXPECT_EQ(config.layout.layoutGuideSheet.calloutGap, 42);
    EXPECT_EQ(config.layout.layoutGuideSheet.leaderStrokeWidth, 3);
    EXPECT_EQ(config.layout.layoutGuideSheet.leaderEndpointDiameter, 7);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesMetricsSectionEntries) {
    const FilePath path = WriteTestConfig(
        "[metrics]\n"
        "cpu.load = *,%,Processor Load\n"
        "gpu.temp = 110,C,GPU Temp\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

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

    RemoveFileIfExists(path);
}

TEST(ConfigParser, IgnoresRuntimePlaceholderMetricMetadataInMetricsSection) {
    const FilePath path = WriteTestConfig(
        "[metrics]\n"
        "nothing = 7,ignored,Overridden Placeholder\n"
        "cpu.load = *,%,Processor Load\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(FindMetricDefinition(config.layout.metrics, "nothing"), nullptr);
    const MetricDefinitionConfig* loadMetric = FindMetricDefinition(config.layout.metrics, "cpu.load");
    ASSERT_NE(loadMetric, nullptr);
    EXPECT_EQ(loadMetric->label, "Processor Load");

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesNamedLayoutSectionsThroughGeneratedSectionTable) {
    const FilePath path = WriteTestConfig(
        "[display]\n"
        "layout = portrait\n"
        "\n"
        "[layout.portrait]\n"
        "description = Portrait Mode\n"
        "window = 480,800\n"
        "cards = columns(cpu,gpu)\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    ASSERT_EQ(config.layout.layouts.size(), 1u);
    EXPECT_EQ(config.layout.layouts[0].name, "portrait");
    EXPECT_EQ(config.layout.layouts[0].description, "Portrait Mode");
    EXPECT_EQ(config.layout.layouts[0].window.width, 480);
    EXPECT_EQ(config.layout.layouts[0].window.height, 800);
    EXPECT_EQ(config.layout.layouts[0].cards.name, "columns");
    ASSERT_EQ(config.layout.layouts[0].cards.children.size(), 2u);
    EXPECT_EQ(config.layout.layouts[0].cards.children[0].name, "cpu");
    EXPECT_EQ(config.layout.layouts[0].cards.children[1].name, "gpu");

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesNamedThemeSectionsThroughGeneratedSectionTable) {
    const FilePath path = WriteTestConfig(
        "[display]\n"
        "theme = dusk\n"
        "\n"
        "[theme.dusk]\n"
        "description = Dusk Contrast\n"
        "background = #101820FF\n"
        "foreground = #F2F5F8FF\n"
        "accent = #FFB000FF\n"
        "guide = #00A6FFFF\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    ASSERT_EQ(config.layout.themes.size(), 1u);
    EXPECT_EQ(config.layout.themes[0].name, "dusk");
    EXPECT_EQ(config.layout.themes[0].description, "Dusk Contrast");
    EXPECT_EQ(config.layout.themes[0].background, 0x101820FFu);
    EXPECT_EQ(config.layout.themes[0].foreground, 0xF2F5F8FFu);
    EXPECT_EQ(config.layout.themes[0].accent, 0xFFB000FFu);
    EXPECT_EQ(config.layout.themes[0].guide, 0x00A6FFFFu);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, ParsesDateTimeWidgetFormatParameters) {
    const FilePath path = WriteTestConfig(
        "[display]\n"
        "layout = test\n"
        "\n"
        "[layout.test]\n"
        "cards = time\n"
        "\n"
        "[card.time]\n"
        "layout = rows(clock_time(HH:MM),clock_date(YYYY-MM-DD))\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    ASSERT_EQ(config.layout.cards.size(), 1u);
    ASSERT_EQ(config.layout.cards[0].layout.children.size(), 2u);
    EXPECT_EQ(config.layout.cards[0].layout.children[0].name, "clock_time");
    EXPECT_EQ(config.layout.cards[0].layout.children[0].parameter, "HH:MM");
    EXPECT_EQ(config.layout.cards[0].layout.children[1].name, "clock_date");
    EXPECT_EQ(config.layout.cards[0].layout.children[1].parameter, "YYYY-MM-DD");

    RemoveFileIfExists(path);
}

TEST(ConfigParser, UsesMetadataOwnedMetricStyleInsteadOfSerializedStyleToken) {
    const FilePath path = WriteTestConfig(
        "[metrics]\n"
        "cpu.load = *,%,Processor Load\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    const MetricDefinitionConfig* loadMetric = FindMetricDefinition(config.layout.metrics, "cpu.load");
    ASSERT_NE(loadMetric, nullptr);
    EXPECT_EQ(loadMetric->style, MetricDisplayStyle::Percent);
    EXPECT_TRUE(loadMetric->telemetryScale);
    EXPECT_EQ(loadMetric->unit, "%");
    EXPECT_EQ(loadMetric->label, "Processor Load");

    RemoveFileIfExists(path);
}

TEST(ConfigParser, RejectsSerializedMetricStyleTokensInMetricsSection) {
    const FilePath path = WriteTestConfig(
        "[metrics]\n"
        "cpu.load = percent,*,%,Processor Load\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(FindMetricDefinition(config.layout.metrics, "cpu.load"), nullptr);

    RemoveFileIfExists(path);
}

TEST(ConfigParser, RejectsUnknownMetricIdsWithoutMetadataStyle) {
    const FilePath path = WriteTestConfig(
        "[metrics]\n"
        "custom.metric = 100,U,Custom\n");

    const AppConfig config = LoadConfig(path, true, TestConfigParseContext());

    EXPECT_EQ(FindMetricDefinition(config.layout.metrics, "custom.metric"), nullptr);

    RemoveFileIfExists(path);
}
