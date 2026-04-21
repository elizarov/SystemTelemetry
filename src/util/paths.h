#pragma once

#include <filesystem>
#include <optional>
#include <string>

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetWorkingDirectory();
std::filesystem::path ResolveExecutableRelativePath(const std::filesystem::path& configuredPath);
std::optional<std::wstring> GetExecutablePath();
