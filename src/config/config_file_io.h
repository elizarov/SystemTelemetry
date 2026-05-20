#pragma once

#include <string>

#include "util/file_path.h"

std::string ReadConfigFile(const FilePath& path);
bool WriteConfigFile(const FilePath& path, const std::string& text);
