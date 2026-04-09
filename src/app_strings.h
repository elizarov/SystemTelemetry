#pragma once

#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

std::string ToLower(std::string value);
std::string Trim(std::string value);
bool ContainsInsensitive(const std::string& value, const std::string& needle);
bool EqualsInsensitive(const std::string& left, const std::string& right);
bool EqualsInsensitive(const std::wstring& left, const std::wstring& right);
std::string JoinNames(const std::vector<std::string>& names);
std::string FormatHresult(HRESULT value);
std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress);
std::string FormatStorageDriveSize(double totalGb);
