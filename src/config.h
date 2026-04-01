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
};

AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveDisplayConfig(
    const std::filesystem::path& path,
    const std::string& monitorName,
    int positionX,
    int positionY);
