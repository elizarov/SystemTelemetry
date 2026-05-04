#pragma once

#include <optional>
#include <string>

std::wstring TrimWhitespace(std::wstring value);
std::wstring StripOuterQuotes(std::wstring value);
std::wstring NormalizeWindowsPath(std::wstring value);
std::wstring QuoteCommandLineArgument(const std::wstring& value);
std::wstring BuildCommandLineExcludingSwitch(const wchar_t* excludedSwitch);
bool HasSwitch(const std::string& target);
std::optional<std::wstring> GetSwitchValue(const std::wstring& target);
std::optional<std::wstring> GetColonSwitchValue(const std::wstring& target);
