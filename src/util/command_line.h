#pragma once

#include <string>

std::wstring TrimWhitespace(std::wstring value);
std::wstring StripOuterQuotes(std::wstring value);
std::wstring NormalizeWindowsPath(std::wstring value);
std::wstring QuoteCommandLineArgument(const std::wstring& value);
