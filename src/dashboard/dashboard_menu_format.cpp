#include "dashboard/dashboard_menu_format.h"

#include "util/text_format.h"

std::string FormatNetworkMenuText(const std::string& adapterName, const std::string& ipAddress) {
    if (adapterName.empty()) {
        return ipAddress;
    }
    if (ipAddress.empty()) {
        return adapterName;
    }
    return FormatText("%s | %s", adapterName.c_str(), ipAddress.c_str());
}

std::string FormatStorageDriveMenuText(const std::string& driveLetter, const std::string& volumeLabel, double totalGb) {
    std::string text = FormatText("%s:", driveLetter.c_str());
    if (!volumeLabel.empty()) {
        AppendFormat(text, " | %s", volumeLabel.c_str());
    }
    AppendFormat(text, " | %s", FormatStorageDriveSize(totalGb).c_str());
    return text;
}

std::string FormatStorageDriveSize(double totalGb) {
    if (totalGb >= 1024.0) {
        return FormatText("%.1f TB", totalGb / 1024.0);
    }
    return FormatText("%.0f GB", totalGb);
}
