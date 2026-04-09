#pragma once

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

struct LayoutMenuOption {
    UINT commandId = 0;
    std::string name;
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
