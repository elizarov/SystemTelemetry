#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using CommandLineArguments = std::vector<std::string>;

CommandLineArguments GetCommandLineArguments();
std::string NormalizeCommandPath(std::string value);
std::string QuoteCommandLineArgument(std::string_view value);
std::string BuildCommandLineExcludingSwitch(const CommandLineArguments& arguments, std::string_view excludedSwitch);
bool HasSwitch(const CommandLineArguments& arguments, std::string_view target);
std::optional<std::string> GetSwitchValue(const CommandLineArguments& arguments, std::string_view target);
std::optional<std::string> GetColonSwitchValue(const CommandLineArguments& arguments, std::string_view target);
