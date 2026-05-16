#pragma once

#include <string>

void AppendHresult(std::string& text, long value);
std::string FormatWin32Error(unsigned long value);
