#pragma once

#include <string>

#include "util/file_path.h"

std::string ReadConfigFileUtf8(const FilePath& path);
bool WriteConfigFileUtf8(const FilePath& path, const std::string& text);
