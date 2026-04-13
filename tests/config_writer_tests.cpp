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
