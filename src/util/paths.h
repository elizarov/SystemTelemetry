#pragma once

#include <optional>

#include "util/file_path.h"

FilePath GetExecutableDirectory();
FilePath GetWorkingDirectory();
FilePath ResolveExecutableRelativePath(const FilePath& configuredPath);
std::optional<FilePath> GetExecutablePath();
