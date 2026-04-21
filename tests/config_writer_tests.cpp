#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "config_parser.h"
#include "config_writer.h"

#include <fstream>
#include <sstream>

namespace {

std::string ReadConfigTemplateFromSourceTree() {
    const std::filesystem::path configPath =
        std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
    std::ifstream input(configPath, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
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

TEST(ConfigWriter, WritesColorAlphaInHexColorValues) {
    AppConfig compareConfig;
    AppConfig currentConfig = compareConfig;
    currentConfig.layout.colors.accentColor = ColorConfig::FromRgba(0x12345678u);

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output, testing::HasSubstr("[colors]\r\n"));
    EXPECT_THAT(output, testing::HasSubstr("accent_color = #12345678\r\n"));
}

TEST(ConfigWriter, FullExportWritesMetricsSectionAndOmitsMetricScales) {
    AppConfig config = LoadConfig(SourceConfigPath(), true);

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
    AppConfig config = LoadConfig(SourceConfigPath(), true);
    config.layout.metrics.definitions.insert(config.layout.metrics.definitions.begin(),
        MetricDefinitionConfig{"nothing", MetricDisplayStyle::Scalar, false, 1.0, "", "Nothing Override"});

    const std::string output = BuildSavedConfigText(
        ReadConfigTemplateFromSourceTree(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    EXPECT_THAT(output, testing::Not(testing::HasSubstr("nothing = 1,,Nothing Override\r\n")));
    EXPECT_THAT(output, testing::Not(testing::HasSubstr("nothing = 1,,Nothing\r\n")));
}

TEST(ConfigWriter, MinimalSavePersistsChangedMetricDefinition) {
    AppConfig compareConfig = LoadConfig(SourceConfigPath(), true);
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
    AppConfig config = LoadConfig(SourceConfigPath(), true);

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

TEST(ConfigWriter, SavesNamedLayoutSectionChangesThroughReflectedDynamicBindings) {
    AppConfig compareConfig = LoadConfig(SourceConfigPath(), true);
    AppConfig currentConfig = compareConfig;

    const auto it = std::find_if(currentConfig.layout.layouts.begin(),
        currentConfig.layout.layouts.end(),
        [](const LayoutSectionConfig& layout) { return layout.name == "3x5"; });
    ASSERT_NE(it, currentConfig.layout.layouts.end());
    it->description = "Portrait Test";
    it->window = {.width = 600, .height = 900};
    it->cardsLayout.name = "columns";
    it->cardsLayout.children.clear();
    it->cardsLayout.children.push_back(LayoutNodeConfig{.name = "cpu"});
    it->cardsLayout.children.push_back(LayoutNodeConfig{.name = "gpu"});

    const std::string output = BuildSavedConfigText(ReadConfigTemplateFromSourceTree(), currentConfig, &compareConfig);

    EXPECT_THAT(output,
        testing::HasSubstr("[layout.3x5]\r\n"
                           "description = Portrait Test\r\n"
                           "window = 600,900\r\n"
                           "cards = columns(cpu,gpu)\r\n"));
}
