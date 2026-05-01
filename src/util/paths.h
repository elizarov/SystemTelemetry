#pragma once

#include <optional>
#include <string>

#include "util/file_path.h"

FilePath GetExecutableDirectory();
FilePath GetWorkingDirectory();
FilePath ResolveExecutableRelativePath(const FilePath& configuredPath);
std::optional<std::wstring> GetExecutablePath();
