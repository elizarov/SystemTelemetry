#pragma once

#include <filesystem>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config.h"

bool SaveConfigElevated(const std::filesystem::path& targetPath, const AppConfig& config, HWND owner);
