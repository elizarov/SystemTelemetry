#pragma once

#include <windows.h>

#include <string_view>

#include "util/file_path.h"

bool IsCurrentProcessElevated();
bool RunElevatedSelf(HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand);
bool RunElevatedSelfAndWait(
    HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand, DWORD* exitCode);
