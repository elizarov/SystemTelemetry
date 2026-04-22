#pragma once

#include <string>
#include <string_view>

bool IsValidUtf8(std::string_view text);
std::wstring WideFromUtf8(std::string_view text);
std::string Utf8FromWide(std::wstring_view text);
std::string Utf8FromAnsi(const char* text);
