#include <gtest/gtest.h>

#include "config_writer.h"

#include <fstream>
#include <sstream>

std::string LoadEmbeddedConfigTemplate() {
    std::ifstream input("resources/config.ini", std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

AppConfig LoadConfig(const std::filesystem::path&, bool) {
    return {};
}

TEST(ConfigWriter, FullExportDoesNotInventEmptyHeaderKeysForHeaderlessCards) {
    AppConfig config;

    LayoutCardConfig card;
    card.id = "storage_usage";
    card.layout.name = "rows";
    card.layout.children.push_back(LayoutNodeConfig{.name = "drive_usage_list"});
    card.layout.children.push_back(LayoutNodeConfig{.name = "vertical_spring"});
    config.layout.cards.push_back(card);

    const std::string output =
        BuildSavedConfigText(LoadEmbeddedConfigTemplate(), config, nullptr, ConfigSaveShape::ExistingTemplateOnly);

    const std::string sectionText =
        "[card.storage_usage]\r\nlayout = rows(drive_usage_list,vertical_spring)\r\n\r\n[card.time]";
    EXPECT_TRUE(output.find(sectionText) != std::string::npos);
    EXPECT_EQ(output.find("[card.storage_usage]\r\nlayout = rows(drive_usage_list,vertical_spring)\r\n\r\ntitle = "),
        std::string::npos);
    EXPECT_EQ(output.find("[card.storage_usage]\r\nlayout = rows(drive_usage_list,vertical_spring)\r\n\r\nicon = "),
        std::string::npos);
}

TEST(ConfigWriter, MinimalSavePersistsResolvedStorageDrivesAgainstEmptySourceConfig) {
    AppConfig compareConfig;
    compareConfig.storage.drives.clear();

    AppConfig currentConfig = compareConfig;
    currentConfig.storage.drives = {"C", "E"};

    const std::string output = BuildSavedConfigText(LoadEmbeddedConfigTemplate(), currentConfig, &compareConfig);

    EXPECT_TRUE(output.find("[storage]\r\ndrives = C,E\r\n") != std::string::npos);
}
