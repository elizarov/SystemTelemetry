#pragma once

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

std::string ToLower(std::string value);
std::string Trim(std::string_view value);
std::vector<std::string> SplitTrimmed(std::string_view value, char delimiter);
std::vector<std::string> SplitTrimmedPreservingEmpty(std::string_view value, char delimiter);
std::string CollapseAsciiWhitespace(std::string_view value);
bool ContainsInsensitive(const std::string& value, const std::string& needle);
bool EqualsInsensitive(const std::string& left, const std::string& right);
std::string JoinNames(const std::vector<std::string>& names);
void SortStrings(std::vector<std::string>& values);
void SortUniqueStrings(std::vector<std::string>& values);
std::string FormatHresult(HRESULT value);
std::string FormatHexColorText(unsigned int value);
std::string FormatDoubleGeneral(double value, int precision = 6);
std::string FormatDoubleFixed(double value, int precision);
std::string FormatDoubleFixedTrimmed(double value, int precision);
std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress);
std::string FormatStorageDriveMenuText(const std::string& driveLetter, const std::string& volumeLabel, double totalGb);
std::string FormatStorageDriveSize(double totalGb);
