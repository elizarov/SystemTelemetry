#include "util/command_line.h"

#include <windows.h>

#include <algorithm>
#include <shellapi.h>
#include <utility>

#include "util/strings.h"
#include "util/utf8.h"

namespace {

char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (LowerAscii(left[i]) != LowerAscii(right[i])) {
            return false;
        }
    }
    return true;
}

bool StartsWithAsciiInsensitive(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && EqualsAsciiInsensitive(value.substr(0, prefix.size()), prefix);
}

std::string StripOuterQuotes(std::string value) {
    value = Trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

}  // namespace

CommandLineArguments GetCommandLineArguments() {
    int count = 0;
    LPWSTR* wideArguments = CommandLineToArgvW(GetCommandLineW(), &count);
    CommandLineArguments arguments;
    if (wideArguments != nullptr) {
        arguments.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            arguments.push_back(Utf8FromWide(wideArguments[i]));
        }
        LocalFree(wideArguments);
    }
    return arguments;
}

std::string NormalizeCommandPath(std::string value) {
    value = StripOuterQuotes(std::move(value));
    std::replace(value.begin(), value.end(), '/', '\\');
    return ToLower(std::move(value));
}

std::string QuoteCommandLineArgument(std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    quoted.append(value);
    quoted.push_back('"');
    return quoted;
}

std::string BuildCommandLineExcludingSwitch(const CommandLineArguments& arguments, std::string_view excludedSwitch) {
    std::string parameters;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (!excludedSwitch.empty() && EqualsAsciiInsensitive(argument, excludedSwitch)) {
            continue;
        }
        if (!parameters.empty()) {
            parameters.push_back(' ');
        }
        parameters += QuoteCommandLineArgument(argument);
    }
    return parameters;
}

bool HasSwitch(const CommandLineArguments& arguments, std::string_view target) {
    for (size_t i = 1; i < arguments.size(); ++i) {
        if (EqualsAsciiInsensitive(arguments[i], target)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> GetSwitchValue(const CommandLineArguments& arguments, std::string_view target) {
    for (size_t i = 1; i + 1 < arguments.size(); ++i) {
        if (EqualsAsciiInsensitive(arguments[i], target)) {
            return arguments[i + 1];
        }
    }
    return std::nullopt;
}

std::optional<std::string> GetColonSwitchValue(const CommandLineArguments& arguments, std::string_view target) {
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument.size() > target.size() && StartsWithAsciiInsensitive(argument, target) &&
            argument[target.size()] == ':') {
            return argument.substr(target.size() + 1);
        }
    }
    return std::nullopt;
}
