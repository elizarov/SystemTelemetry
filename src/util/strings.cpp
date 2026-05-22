#include "util/strings.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "util/text_format.h"

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
        const std::string trimmed = Trim(delimiterIndex == std::string_view::npos ? value.substr(start) :
            value.substr(start, delimiterIndex - start));
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
        parts.push_back(Trim(delimiterIndex == std::string_view::npos ? value.substr(start) :
            value.substr(start, delimiterIndex - start)));
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

std::string JoinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            AppendFormat(joined, ",");
        }
        AppendFormat(joined, "%s", names[i].c_str());
    }
    return joined;
}

size_t StableStringHash(std::string_view value) {
    size_t hash = static_cast<size_t>(14695981039346656037ull);
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= static_cast<size_t>(1099511628211ull);
    }
    return hash;
}

void SortStrings(std::vector<std::string>& values) {
    // Size: keep string sorting in one concrete helper instead of re-instantiating std::sort at call sites.
    for (size_t i = 1; i < values.size(); ++i) {
        std::string value = std::move(values[i]);
        size_t insert = i;
        while (insert > 0 && value < values[insert - 1]) {
            values[insert] = std::move(values[insert - 1]);
            --insert;
        }
        values[insert] = std::move(value);
    }
}

void SortUniqueStrings(std::vector<std::string>& values) {
    SortStrings(values);
    size_t out = 0;
    for (auto& value : values) {
        if (out != 0 && values[out - 1] == value) {
            continue;
        }
        values[out] = std::move(value);
        ++out;
    }
    values.resize(out);
}
