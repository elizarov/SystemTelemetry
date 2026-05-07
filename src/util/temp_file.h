#pragma once

#include <string_view>

#include "util/file_path.h"

FilePath CreateTempFilePath(std::string_view prefix);
