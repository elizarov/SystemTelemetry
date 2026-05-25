#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using CommandLineArguments = std::vector<std::string>;

struct CommandLineColonSwitchValue {
    size_t index = 0;
    std::string_view value;
};

CommandLineArguments GetCommandLineArguments();
std::string NormalizeCommandPath(std::string value);
std::string QuoteCommandLineArgument(std::string_view value);
std::string BuildCommandLineExcludingSwitch(const CommandLineArguments& arguments, std::string_view excludedSwitch);
std::optional<size_t> FindSwitchIndex(const CommandLineArguments& arguments, std::string_view target);
bool HasSwitch(const CommandLineArguments& arguments, std::string_view target);
std::optional<std::string> GetSwitchValue(const CommandLineArguments& arguments, std::string_view target);
std::optional<CommandLineColonSwitchValue> GetColonSwitchValueWithIndex(
    const CommandLineArguments& arguments, std::string_view target);
std::optional<std::string> GetColonSwitchValue(const CommandLineArguments& arguments, std::string_view target);
