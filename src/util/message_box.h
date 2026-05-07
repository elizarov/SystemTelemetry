#pragma once

#include <string_view>

struct HWND__;
using HWND = HWND__*;
using UINT = unsigned int;

int MessageBoxUtf8(HWND owner, std::string_view text, UINT type);
int MessageBoxUtf8(std::string_view text, UINT type);
