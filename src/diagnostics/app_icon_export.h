#pragma once

#include <string>

#include "util/file_path.h"

struct AppConfig;

bool SaveAppIconPng(const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText = nullptr);
