#pragma once

#include <windows.h>

#include <string_view>

#include "util/file_path.h"

bool RunElevatedSelfAndWait(
    HWND owner, std::string_view parameters, const FilePath& workingDirectory, int showCommand, DWORD* exitCode);
