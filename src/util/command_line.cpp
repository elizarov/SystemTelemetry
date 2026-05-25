#include "util/command_line.h"

#include <windows.h>

#include <algorithm>
#include <shellapi.h>
#include <utility>

#include "util/strings.h"
#include "util/text_encoding.h"
#include "util/text_format.h"

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
            arguments.push_back(TextFromWide(wideArguments[i]));
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
    return FormatText("\"%.*s\"", static_cast<int>(value.size()), value.data());
}

std::string BuildCommandLineExcludingSwitch(const CommandLineArguments& arguments, std::string_view excludedSwitch) {
    std::string parameters;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (!excludedSwitch.empty() && EqualsAsciiInsensitive(argument, excludedSwitch)) {
            continue;
        }
        if (!parameters.empty()) {
            AppendFormat(parameters, " ");
        }
        AppendFormat(parameters, "%s", QuoteCommandLineArgument(argument).c_str());
    }
    return parameters;
}

bool HasSwitch(const CommandLineArguments& arguments, std::string_view target) {
    return FindSwitchIndex(arguments, target).has_value();
}

std::optional<size_t> FindSwitchIndex(const CommandLineArguments& arguments, std::string_view target) {
    for (size_t i = 1; i < arguments.size(); ++i) {
        if (EqualsAsciiInsensitive(arguments[i], target)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::string> GetSwitchValue(const CommandLineArguments& arguments, std::string_view target) {
    for (size_t i = 1; i + 1 < arguments.size(); ++i) {
        if (EqualsAsciiInsensitive(arguments[i], target)) {
            return arguments[i + 1];
        }
    }
    return std::nullopt;
}

std::optional<CommandLineColonSwitchValue> GetColonSwitchValueWithIndex(
    const CommandLineArguments& arguments, std::string_view target) {
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument.size() > target.size() && StartsWithAsciiInsensitive(argument, target) &&
            argument[target.size()] == ':') {
            return CommandLineColonSwitchValue{i, std::string_view(argument).substr(target.size() + 1)};
        }
    }
    return std::nullopt;
}

std::optional<std::string> GetColonSwitchValue(const CommandLineArguments& arguments, std::string_view target) {
    if (const auto value = GetColonSwitchValueWithIndex(arguments, target); value.has_value()) {
        return std::string(value->value);
    }
    return std::nullopt;
}
