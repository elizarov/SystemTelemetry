#pragma once

#include <optional>
#include <string>

std::wstring TrimWhitespace(std::wstring value);
std::wstring StripOuterQuotes(std::wstring value);
std::wstring NormalizeWindowsPath(std::wstring value);
std::wstring QuoteCommandLineArgument(const std::wstring& value);
std::wstring BuildCommandLineExcludingSwitch(const wchar_t* excludedSwitch);
bool HasSwitch(const wchar_t* target);
std::optional<std::wstring> GetSwitchValue(const wchar_t* target);
std::optional<std::wstring> GetColonSwitchValue(const wchar_t* target);
