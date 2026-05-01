#pragma once

#include <string>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "util/file_path.h"

enum class ConfigSaveShape {
    UpdateOrAppend,
    ExistingTemplateOnly,
};

std::string FormatLayoutExpression(const LayoutNodeConfig& node);
std::string BuildSavedConfigText(const std::string& initialText,
    const AppConfig& config,
    const AppConfig* compareConfig,
    ConfigSaveShape shape = ConfigSaveShape::UpdateOrAppend);
bool SaveConfig(const FilePath& path, const AppConfig& config, const ConfigParseContext& context);
bool SaveFullConfig(const FilePath& path, const AppConfig& config);
