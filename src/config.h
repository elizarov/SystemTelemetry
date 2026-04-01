#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AppConfig {
    std::wstring monitorName;
    int positionX = 0;
    int positionY = 0;
    std::wstring networkAdapter;
    std::vector<std::wstring> driveLetters;
};

AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveDisplayConfig(
    const std::filesystem::path& path,
    const std::wstring& monitorName,
    int positionX,
    int positionY);
