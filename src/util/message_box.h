#pragma once

#include <string_view>

struct HWND__;
using HWND = HWND__*;
using UINT = unsigned int;

int ShowAppMessageBox(HWND owner, std::string_view text, UINT type);
int ShowAppMessageBox(std::string_view text, UINT type);
