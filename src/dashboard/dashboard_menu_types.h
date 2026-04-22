#pragma once

#include <windows.h>

#include <string>

struct LayoutMenuOption {
    UINT commandId = 0;
    std::string name;
    std::string description;
};

struct NetworkMenuOption {
    UINT commandId = 0;
    std::string adapterName;
    std::string ipAddress;
    bool selected = false;
};

struct StorageDriveMenuOption {
    UINT commandId = 0;
    std::string driveLetter;
    std::string volumeLabel;
    double totalGb = 0.0;
    bool selected = false;
};

struct ScaleMenuOption {
    UINT commandId = 0;
    double scale = 0.0;
    std::string label;
    bool selected = false;
    bool isDefault = false;
    bool isCustomEntry = false;
};
