#include "util/strings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Trim(std::string_view value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

std::vector<std::string> SplitTrimmed(std::string_view value, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t delimiterIndex = value.find(delimiter, start);
        const std::string trimmed =
            Trim(delimiterIndex == std::string_view::npos ? value.substr(start)
                                                          : value.substr(start, delimiterIndex - start));
        if (!trimmed.empty()) {
            parts.push_back(trimmed);
        }
        if (delimiterIndex == std::string_view::npos) {
            break;
        }
        start = delimiterIndex + 1;
    }
    return parts;
}

std::vector<std::string> SplitTrimmedPreservingEmpty(std::string_view value, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t delimiterIndex = value.find(delimiter, start);
        parts.push_back(Trim(delimiterIndex == std::string_view::npos ? value.substr(start)
                                                                      : value.substr(start, delimiterIndex - start)));
        if (delimiterIndex == std::string_view::npos) {
            break;
        }
        start = delimiterIndex + 1;
    }
    return parts;
}

std::string CollapseAsciiWhitespace(std::string_view value) {
    std::string collapsed;
    collapsed.reserve(value.size());

    bool pendingSpace = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch) != 0) {
            pendingSpace = !collapsed.empty();
            continue;
        }
        if (pendingSpace) {
            collapsed.push_back(' ');
            pendingSpace = false;
        }
        collapsed.push_back(static_cast<char>(ch));
    }
    return collapsed;
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
        sprintf_s(buffer, "%.1f TB", totalGb / 1024.0);
    } else {
        sprintf_s(buffer, "%.0f GB", totalGb);
    }
    return buffer;
}
