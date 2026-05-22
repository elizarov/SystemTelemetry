#pragma once

#include <string>
#include <string_view>

std::wstring WideFromText(std::string_view text);
std::string TextFromWide(std::wstring_view text);
bool IsValidUtf8(std::string_view text);
