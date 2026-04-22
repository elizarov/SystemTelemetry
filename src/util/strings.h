#pragma once

#include <windows.h>

#include <string>
#include <vector>

std::string ToLower(std::string value);
std::string Trim(std::string value);
bool ContainsInsensitive(const std::string& value, const std::string& needle);
bool EqualsInsensitive(const std::string& left, const std::string& right);
bool EqualsInsensitive(const std::wstring& left, const std::wstring& right);
std::string JoinNames(const std::vector<std::string>& names);
std::string FormatHresult(HRESULT value);
std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress);
std::string FormatStorageDriveMenuText(const std::string& driveLetter, const std::string& volumeLabel, double totalGb);
std::string FormatStorageDriveSize(double totalGb);
