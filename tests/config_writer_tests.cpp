#include <algorithm>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <sstream>

#include "config/color_resolver.h"
#include "config/config_parser.h"
#include "config/config_telemetry.h"
#include "config/config_writer.h"
#include "telemetry/metrics.h"

namespace {

std::string ReadConfigTemplateFromSourceTree() {
    const FilePath configPath = FilePath(CASEDASH_SOURCE_DIR) / "resources" / "config.ini";
    std::ifstream input(configPath.string(), std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

FilePath SourceConfigPath() {
    return FilePath(CASEDASH_SOURCE_DIR) / "resources" / "config.ini";
}

ConfigParseContext TestConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

}  // namespace

TEST(ConfigWriter, FullExportDoesNotInventEmptyHeaderKeysForHeaderlessCards) {
    AppConfig config;

    LayoutCardConfig card;
    card.id = "storage_usage";
    card.layout.name = "rows";
    card.layout.children.push_back(LayoutNodeConfig{.name = "drive_usage_list"});
    card.layout.children.push_back(LayoutNodeConfig{.name = "vertical_spring"});
    config.layout.cards.push_back(card);

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    const std::string sectionText =
        "[card.storage_usage]\r\nlayout = rows(drive_usage_list,vertical_spring)\r\n\r\n[card.time]";
    EXPECT_THAT(output, testing::HasSubstr(sectionText));
    EXPECT_THAT(output,
        testing::Not(testing::HasSubstr(
            "[card.storage_usage]\r\nlayout = rows(drive_usage_list,vertical_spring)\r\n\r\ntitle = ")));
    EXPECT_THAT(output,
        testing::Not(testing::HasSubstr(
            "[card.storage_usage]\r\nlayout = rows(drive_usage_list,vertical_spring)\r\n\r\nicon = ")));
}

TEST(ConfigWriter, MinimalSavePersistsResolvedStorageDrivesAgainstEmptySourceConfig) {
    AppConfig compareConfig;
    compareConfig.storage.drives.clear();

    AppConfig currentConfig = compareConfig;
    currentConfig.storage.drives = {"C", "E"};

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("[storage]\r\ndrives = C,E\r\n"));
}

TEST(ConfigWriter, MinimalSavePersistsResolvedNetworkAdapterAgainstEmptySourceConfig) {
    AppConfig compareConfig;
    compareConfig.network.adapterName.clear();

    AppConfig currentConfig = compareConfig;
    currentConfig.network.adapterName = "Ethernet";

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("[network]\r\nadapter_name = Ethernet\r\n"));
}

TEST(ConfigWriter, MinimalSavePersistsGpuAdapterAgainstEmptySourceConfig) {
    AppConfig compareConfig;
    compareConfig.gpu.adapterName.clear();

    AppConfig currentConfig = compareConfig;
    currentConfig.gpu.adapterName = "NVIDIA GeForce RTX 4070 Laptop GPU";

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("[gpu]\r\nadapter_name = NVIDIA GeForce RTX 4070 Laptop GPU\r\n"));
}

TEST(ConfigWriter, MinimalSaveInsertsMissingGpuSectionWithKeyBeforeSectionSeparator) {
    AppConfig compareConfig;
    compareConfig.gpu.adapterName.clear();
    compareConfig.network.adapterName = "Wi-Fi";
    compareConfig.storage.drives = {"C"};

    AppConfig currentConfig = compareConfig;
    currentConfig.gpu.adapterName = "NVIDIA GeForce RTX 4070 Laptop GPU";

    const std::string initialText = "[display]\r\n"
                                    "monitor_name = TL160ADMP03-0\r\n"
                                    "position = 258,117\r\n"
                                    "scale = 2\r\n"
                                    "\r\n"
                                    "[network]\r\n"
                                    "adapter_name = Wi-Fi\r\n"
                                    "\r\n"
                                    "[storage]\r\n"
                                    "drives = C\r\n";

    const std::string output = BuildSavedConfigText(initialText, currentConfig, &compareConfig);

    EXPECT_THAT(output,
        testing::HasSubstr("[display]\r\n"
                           "monitor_name = TL160ADMP03-0\r\n"
                           "position = 258,117\r\n"
                           "scale = 2\r\n"
                           "\r\n"
                           "[gpu]\r\n"
                           "adapter_name = NVIDIA GeForce RTX 4070 Laptop GPU\r\n"
                           "\r\n"
                           "[network]\r\n"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("[gpu]\r\n\r\nadapter_name")));
}

TEST(ConfigWriter, MinimalSavePersistsResolvedBoardBindingsAgainstEmptySourceConfig) {
    AppConfig compareConfig;
    compareConfig.layout.board.requestedTemperatureNames = {"cpu"};
    compareConfig.layout.board.requestedFanNames = {"cpu", "gpu", "system"};
    compareConfig.layout.board.temperatureSensorNames["cpu"] = "";
    compareConfig.layout.board.fanSensorNames["cpu"] = "";
    compareConfig.layout.board.fanSensorNames["gpu"] = "";
    compareConfig.layout.board.fanSensorNames["system"] = "";

    AppConfig currentConfig = compareConfig;
    currentConfig.layout.board.temperatureSensorNames["cpu"] = "CPU";
    currentConfig.layout.board.fanSensorNames["cpu"] = "CPU";
    currentConfig.layout.board.fanSensorNames["gpu"] = "GPU Fan";
    currentConfig.layout.board.fanSensorNames["system"] = "System 1";

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("[board]\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("board.temp.cpu = CPU\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("board.fan.cpu = CPU\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("board.fan.gpu = GPU Fan\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("board.fan.system = System 1\r\n"));
}

TEST(ConfigWriter, WritesColorAlphaInHexColorValues) {
    AppConfig compareConfig;
    AppConfig currentConfig = compareConfig;
    currentConfig.layout.colors.accentColor = ColorConfig::FromRgba(0x12345678u);

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("[colors]\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("accent_color = #12345678\r\n"));
}

TEST(ConfigWriter, WritesDerivedColorExpressions) {
    AppConfig compareConfig;
    AppConfig currentConfig = compareConfig;
    currentConfig.layout.colors.accentColor = ColorConfig::FromRgba(0x00BFFFFFu);
    currentConfig.layout.colors.accentColor.expression = "accent";
    currentConfig.layout.colors.peakGhostColor = ColorConfig::FromRgba(0x00BFFF60u);
    currentConfig.layout.colors.peakGhostColor.expression = "accent(alpha: 0x60)";

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("accent_color = accent\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("peak_ghost_color = accent(alpha: 0x60)\r\n"));
}

TEST(ConfigWriter, MinimalSaveIgnoresThemeResolvedColorChangesWhenExpressionsAreUnchanged) {
    AppConfig compareConfig = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    std::optional<AppConfig> changedThemeConfig;
    for (const ThemeConfig& theme : compareConfig.layout.themes) {
        AppConfig candidateConfig = compareConfig;
        candidateConfig.display.theme = theme.name;
        ResolveConfiguredColors(candidateConfig);
        if (candidateConfig.layout.colors.backgroundColor.ToRgba() !=
            compareConfig.layout.colors.backgroundColor.ToRgba()) {
            changedThemeConfig = std::move(candidateConfig);
            break;
        }
    }
    ASSERT_TRUE(changedThemeConfig.has_value());

    const AppConfig& currentConfig = *changedThemeConfig;
    ASSERT_NE(
        currentConfig.layout.colors.backgroundColor.ToRgba(), compareConfig.layout.colors.backgroundColor.ToRgba());
    ASSERT_EQ(
        currentConfig.layout.colors.backgroundColor.expression, compareConfig.layout.colors.backgroundColor.expression);

    const std::string output = BuildSavedConfigText(
        "[display]\r\ntheme = " + compareConfig.display.theme + "\r\n", currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("theme = " + currentConfig.display.theme + "\r\n"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("[colors]\r\n")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("background_color = ")));
}

TEST(ConfigWriter, MinimalSaveDoesNotEmitLeadingEmptyLineForEmptyInitialText) {
    AppConfig compareConfig;
    AppConfig currentConfig = compareConfig;
    currentConfig.display.theme = "light_blue";

    const std::string output = BuildSavedConfigText("", currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::StartsWith("[display]\r\n"));
    EXPECT_THAT(output, testing::Not(testing::StartsWith("\r\n")));
}

TEST(ConfigWriter, LayoutConfigDifferenceCheckUsesMetadataAndSelectedStructure) {
    LayoutConfig saved;
    saved.structure.cards = LayoutNodeConfig{.name = "rows"};

    LayoutConfig current = saved;
    EXPECT_FALSE(LayoutConfigHasDifferences(current, saved));

    current.dashboard.outerMargin = saved.dashboard.outerMargin + 1;
    EXPECT_TRUE(LayoutConfigHasDifferences(current, saved));

    current = saved;
    current.structure.cards.name = "columns";
    EXPECT_TRUE(LayoutConfigHasDifferences(current, saved));
}

TEST(ConfigWriter, MinimalSavePreservesLeadingCommentsWithoutLeadingEmptyLine) {
    AppConfig compareConfig;
    AppConfig currentConfig = compareConfig;
    currentConfig.display.theme = "light_blue";

    const std::string output = BuildSavedConfigText("\r\n; local overrides\r\n", currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::StartsWith("; local overrides\r\n\r\n[display]\r\n"));
}

TEST(ConfigWriter, FullExportWritesThemeSections) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    EXPECT_THAT(output,
        testing::HasSubstr("[display]\r\n"
                           "monitor_name = \r\n"
                           "layout = 5x3\r\n"
                           "theme = dark_cyan\r\n"));
    EXPECT_THAT(output,
        testing::HasSubstr("[theme.dark_cyan]\r\n"
                           "description = Black, white, cyan\r\n"
                           "background = #000000FF\r\n"
                           "foreground = #FFFFFFFF\r\n"
                           "accent = #00BFFFFF\r\n"
                           "guide = #FF6A00FF\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("panel_border_color = background(mix: 0.34 accent)\r\n"));
}

TEST(ConfigWriter, FullExportWritesMetricsSectionAndOmitsMetricScales) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());

    const MetricDefinitionConfig* cpuLoad = FindMetricDefinition(config.layout.metrics, "cpu.load");
    ASSERT_NE(cpuLoad, nullptr);

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    EXPECT_THAT(output, testing::HasSubstr("[metrics]\r\ncpu.load = *,%,Load\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("network.upload = *,MB/s,Up\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("drive.free = *,GB|TB,Free\r\n"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("nothing = 1,,Nothing\r\n")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("[metric_scales]")));
}

TEST(ConfigWriter, FullExportOmitsRuntimePlaceholderMetricDefinition) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    config.layout.metrics.definitions.insert(config.layout.metrics.definitions.begin(),
        MetricDefinitionConfig{"nothing", MetricDisplayStyle::Scalar, false, 1.0, "", "Nothing Override"});

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    EXPECT_THAT(output, testing::Not(testing::HasSubstr("nothing = 1,,Nothing Override\r\n")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("nothing = 1,,Nothing\r\n")));
}

TEST(ConfigWriter, MinimalSavePersistsChangedMetricDefinition) {
    AppConfig compareConfig = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    AppConfig currentConfig = compareConfig;

    MetricDefinitionConfig* gpuTemp = FindMetricDefinition(currentConfig.layout.metrics, "gpu.temp");
    ASSERT_NE(gpuTemp, nullptr);
    gpuTemp->label = "Core Temp";

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output,
        testing::HasSubstr("gpu.temp = 100,\xC2\xB0"
                           "C,Core Temp\r\n"));
}

TEST(ConfigWriter, SerializedMetricStyleComesFromMetadataInsteadOfStructValue) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());

    MetricDefinitionConfig* gpuTemp = FindMetricDefinition(config.layout.metrics, "gpu.temp");
    ASSERT_NE(gpuTemp, nullptr);
    gpuTemp->style = MetricDisplayStyle::Percent;

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    EXPECT_THAT(output,
        testing::HasSubstr("gpu.temp = 100,\xC2\xB0"
                           "C,Temp\r\n"));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("gpu.temp = percent,100,")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("gpu.temp = scalar,100,")));
}

TEST(ConfigWriter, SavesNamedLayoutSectionChangesThroughGeneratedSectionTable) {
    AppConfig compareConfig = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    AppConfig currentConfig = compareConfig;

    const auto it = std::find_if(currentConfig.layout.layouts.begin(),
        currentConfig.layout.layouts.end(),
        [](const LayoutSectionConfig& layout) { return layout.name == "3x5"; });
    ASSERT_NE(it, currentConfig.layout.layouts.end());
    it->description = "Portrait Test";
    it->window = {.width = 600, .height = 900};
    it->cards.name = "columns";
    it->cards.children.clear();
    it->cards.children.push_back(LayoutNodeConfig{.name = "cpu"});
    it->cards.children.push_back(LayoutNodeConfig{.name = "gpu"});

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output,
        testing::HasSubstr("[layout.3x5]\r\n"
                           "description = Portrait Test\r\n"
                           "window = 600,900\r\n"
                           "cards = columns(cpu,gpu)\r\n"));
}

TEST(ConfigWriter, SavesNamedThemeSectionChangesThroughGeneratedSectionTable) {
    AppConfig compareConfig = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());
    AppConfig currentConfig = compareConfig;

    const auto it = std::find_if(currentConfig.layout.themes.begin(),
        currentConfig.layout.themes.end(),
        [](const ThemeConfig& theme) { return theme.name == "dark_cyan"; });
    ASSERT_NE(it, currentConfig.layout.themes.end());
    it->description = "Test theme description";
    it->accent = ColorConfig::FromRgba(0x123456FFu);

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output,
        testing::HasSubstr("[theme.dark_cyan]\r\n"
                           "description = Test theme description\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("accent = #123456FF\r\n"));
}

TEST(ConfigWriter, PreservesDateTimeWidgetFormatParameters) {
    AppConfig config;
    LayoutCardConfig card;
    card.id = "time";
    card.layout.name = "rows";
    LayoutNodeConfig timeNode;
    timeNode.name = "clock_time";
    timeNode.parameter = "HH:MM";
    LayoutNodeConfig dateNode;
    dateNode.name = "clock_date";
    dateNode.parameter = "YYYY-MM-DD";
    card.layout.children.push_back(timeNode);
    card.layout.children.push_back(dateNode);
    config.layout.cards.push_back(card);

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    EXPECT_THAT(output, testing::HasSubstr("layout = rows(clock_time(HH:MM),clock_date(YYYY-MM-DD))\r\n"));
}
