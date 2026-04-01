#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct SensorBinding {
    std::vector<std::wstring> namespaces;
    std::wstring matchField;
    std::wstring matchValue;
    std::wstring valueField;

    bool IsConfigured() const {
        return !matchField.empty() && !matchValue.empty() && !valueField.empty();
    }
};

struct AppConfig {
    std::wstring monitorName;
    int positionX = 0;
    int positionY = 0;
    std::wstring networkAdapter;
    std::vector<std::wstring> driveLetters;
    std::map<std::wstring, SensorBinding> sensors;
};

AppConfig LoadConfig(const std::filesystem::path& path);
bool SaveDisplayConfig(
    const std::filesystem::path& path,
    const std::wstring& monitorName,
    int positionX,
    int positionY);
