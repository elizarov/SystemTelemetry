#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct UiFontConfig {
    std::string face;
    int size = 0;
    int weight = 0;
};

struct LayoutNodeConfig {
    std::string name;
    int weight = 1;
    std::vector<std::pair<std::string, std::string>> parameters;
    std::vector<LayoutNodeConfig> children;
};

struct LayoutCardConfig {
    std::string id;
    std::string title;
    std::string icon;
    LayoutNodeConfig layout;
};

struct LayoutConfig {
    int windowWidth = 800;
    int windowHeight = 480;
    unsigned int backgroundColor = 0x000000;
    unsigned int foregroundColor = 0xFFFFFF;
    unsigned int accentColor = 0x00BFFF;
    unsigned int panelBorderColor = 0xEBEBEB;
    unsigned int mutedTextColor = 0xA5B4BE;
    unsigned int trackColor = 0x2D343A;
    unsigned int panelFillColor = 0x06080B;
    unsigned int graphBackgroundColor = 0x0A0C0F;
    unsigned int graphGridColor = 0x2A3036;
    unsigned int graphAxisColor = 0x505860;

    int outerMargin = 10;
    int rowGap = 10;
    int cardGap = 10;
    int cardPadding = 16;
    int cardRadius = 18;
    int cardBorderWidth = 1;
    int headerHeight = 28;
    int headerIconSize = 20;
    int headerGap = 10;
    int contentGap = 12;
    int columnGap = 16;
    int metricRowHeight = 34;
    int metricLabelWidth = 74;
    int metricValueGap = 8;
    int metricBarHeight = 4;
    int widgetLineGap = 8;
    int driveRowHeight = 18;
    int driveLabelWidth = 28;
    int drivePercentWidth = 46;
    int driveFreeWidth = 72;
    int driveBarGap = 8;
    int driveValueGap = 10;
    int driveBarHeight = 12;
    int throughputAxisWidth = 18;
    int throughputHeaderGap = 2;
    int throughputReadLabelWidth = 54;
    int throughputWriteLabelWidth = 42;
    int throughputGraphHeight = 41;
    int gaugePreferredSize = 120;

    UiFontConfig titleFont{"Segoe UI Semibold", 18, 700};
    UiFontConfig bigFont{"Segoe UI Semibold", 40, 700};
    UiFontConfig valueFont{"Segoe UI Semibold", 17, 700};
    UiFontConfig labelFont{"Segoe UI", 14, 400};
    UiFontConfig smallFont{"Segoe UI", 12, 400};

    LayoutNodeConfig cardsLayout;
    std::vector<LayoutCardConfig> cards;
};

struct AppConfig {
    std::string monitorName;
    int positionX = 0;
    int positionY = 0;
    std::string networkAdapter;
    std::vector<std::string> driveLetters;
    std::string gigabyteFanChannelName;
    std::string gigabyteTemperatureChannelName;
    LayoutConfig layout;
};

std::string LoadEmbeddedConfigTemplate();
AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
std::vector<std::string> CollectLayoutDriveLetters(const LayoutConfig& layout);
