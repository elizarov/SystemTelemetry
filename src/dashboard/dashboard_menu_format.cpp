#include "dashboard/dashboard_menu_format.h"

#include <cstdio>

std::string FormatNetworkMenuText(const std::string& adapterName, const std::string& ipAddress) {
    if (adapterName.empty()) {
        return ipAddress;
    }
    if (ipAddress.empty()) {
        return adapterName;
    }
    return adapterName + " | " + ipAddress;
}

std::string FormatStorageDriveMenuText(const std::string& driveLetter, const std::string& volumeLabel, double totalGb) {
    std::string text = driveLetter + ":";
    if (!volumeLabel.empty()) {
        text += " | " + volumeLabel;
    }
    text += " | " + FormatStorageDriveSize(totalGb);
    return text;
}

std::string FormatStorageDriveSize(double totalGb) {
    char buffer[64];
    if (totalGb >= 1024.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f TB", totalGb / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f GB", totalGb);
    }
    return buffer;
}
