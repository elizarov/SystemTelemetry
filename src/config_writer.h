#pragma once

#include <filesystem>
#include <string>

#include "config.h"

enum class ConfigSaveShape {
    UpdateOrAppend,
    ExistingTemplateOnly,
};

std::string FormatLayoutExpression(const LayoutNodeConfig& node);
std::string BuildSavedConfigText(const std::string& initialText,
    const AppConfig& config,
    const AppConfig* compareConfig,
    ConfigSaveShape shape = ConfigSaveShape::UpdateOrAppend);
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);
bool SaveFullConfig(const std::filesystem::path& path, const AppConfig& config);
