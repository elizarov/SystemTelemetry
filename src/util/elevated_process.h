#pragma once

#include <windows.h>

bool RunElevatedSelfAndWait(
    HWND owner, const wchar_t* parameters, const wchar_t* workingDirectory, int showCommand, DWORD* exitCode);
