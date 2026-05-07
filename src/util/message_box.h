#pragma once

#include <windows.h>

#include <string_view>

int MessageBoxUtf8(HWND owner, std::string_view text, UINT type);
int MessageBoxUtf8(std::string_view text, UINT type);
