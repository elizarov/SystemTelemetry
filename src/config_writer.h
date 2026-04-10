#pragma once

#include <filesystem>
#include <string>

#include "config.h"

std::string FormatLayoutExpression(const LayoutNodeConfig& node);
std::string BuildSavedConfigText(
    const std::string& initialText, const AppConfig& config, const AppConfig* compareConfig);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
bool SaveFullConfig(const std::filesystem::path& path, const AppConfig& config);
