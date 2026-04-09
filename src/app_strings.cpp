#include "app_strings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

bool ContainsInsensitive(const std::string& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return ToLower(value).find(ToLower(needle)) != std::string::npos;
}

bool EqualsInsensitive(const std::string& left, const std::string& right) {
    return ToLower(left) == ToLower(right);
}

bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (::towlower(left[i]) != ::towlower(right[i])) {
            return false;
        }
    }
    return true;
}

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            joined += ",";
        }
        joined += names[i];
    }
    return joined;
}

std::string FormatHresult(HRESULT value) {
    char buffer[32];
    sprintf_s(buffer, "0x%08lX", static_cast<unsigned long>(value));
    return buffer;
}

std::string FormatNetworkFooterText(const std::string& adapterName, const std::string& ipAddress) {
    if (adapterName.empty()) {
        return ipAddress;
    }
    if (ipAddress.empty()) {
        return adapterName;
    }
    return adapterName + " | " + ipAddress;
}

std::string FormatStorageDriveSize(double totalGb) {
    char buffer[64];
    if (totalGb >= 1024.0) {
        sprintf_s(buffer, "%.1f TB", totalGb / 1024.0);
    } else {
        sprintf_s(buffer, "%.0f GB", totalGb);
    }
    return buffer;
}
