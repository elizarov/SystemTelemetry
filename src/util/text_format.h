#pragma once

#include <cstdarg>
#include <string>

std::string FormatTextV(const char* format, va_list args);
std::string FormatText(const char* format, ...);
void AssignFormat(std::string& text, const char* format, ...);
void AppendFormat(std::string& text, const char* format, ...);
