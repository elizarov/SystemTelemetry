#include "util/command_line.h"

#include <algorithm>

std::wstring TrimWhitespace(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return iswspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::wstring(first, last);
}

std::wstring StripOuterQuotes(std::wstring value) {
    value = TrimWhitespace(std::move(value));
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::wstring NormalizeWindowsPath(std::wstring value) {
    value = StripOuterQuotes(std::move(value));
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(
        value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring QuoteCommandLineArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}
