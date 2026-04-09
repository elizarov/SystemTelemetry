#pragma once

#include <filesystem>
#include <optional>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "config.h"
#include "telemetry_runtime.h"

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetWorkingDirectory();
std::filesystem::path ResolveExecutableRelativePath(const std::filesystem::path& configuredPath);
std::optional<std::wstring> GetExecutablePath();
std::filesystem::path GetRuntimeConfigPath();
AppConfig LoadRuntimeConfig(const DiagnosticsOptions& options);
