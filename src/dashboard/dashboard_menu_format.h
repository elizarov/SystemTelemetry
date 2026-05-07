#pragma once

#include <string>

std::string FormatNetworkMenuText(const std::string& adapterName, const std::string& ipAddress);
std::string FormatStorageDriveMenuText(const std::string& driveLetter, const std::string& volumeLabel, double totalGb);
std::string FormatStorageDriveSize(double totalGb);
