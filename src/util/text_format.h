#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

enum class ResourceStringId : std::uint32_t;

std::string FormatTextV(const char* format, va_list args);
std::string FormatTextV(ResourceStringId format, va_list args);
std::string FormatText(const char* format, ...);
std::string FormatText(ResourceStringId format, ...);
void AssignFormat(std::string& text, const char* format, ...);
void AssignFormat(std::string& text, ResourceStringId format, ...);
void AppendFormat(std::string& text, const char* format, ...);
void AppendFormat(std::string& text, ResourceStringId format, ...);
