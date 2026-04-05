#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct UiFontConfig {
    std::string face;
    int size = 0;
    int weight = 0;
};

struct LayoutConfig {
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

    UiFontConfig titleFont{"Segoe UI Semibold", 18, 700};
    UiFontConfig bigFont{"Segoe UI Semibold", 40, 700};
    UiFontConfig valueFont{"Segoe UI Semibold", 17, 700};
    UiFontConfig labelFont{"Segoe UI", 14, 400};
    UiFontConfig smallFont{"Segoe UI", 12, 400};
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
