#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AppConfig {
    std::string monitorName;
    int positionX = 0;
    int positionY = 0;
    std::string networkAdapter;
    std::vector<std::string> driveLetters;
    std::string gigabyteFanChannelName;
    std::string gigabyteTemperatureChannelName;
};

std::string LoadEmbeddedConfigTemplate();
AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
