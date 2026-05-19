#pragma once

#include <string>

#include "config/metric_catalog.h"
#include "util/file_path.h"

struct AppConfig;
struct LayoutConfig;

enum class ConfigSaveShape {
    UpdateOrAppend,
    ExistingTemplateOnly,
};

std::string BuildSavedConfigText(const std::string& initialText,
    const AppConfig& config,
    const AppConfig* compareConfig,
    ConfigSaveShape shape = ConfigSaveShape::UpdateOrAppend);
bool LayoutConfigHasDifferences(const LayoutConfig& config, const LayoutConfig& compareConfig);
bool SaveConfig(const FilePath& path, const AppConfig& config, const ConfigParseContext& context);
bool SaveFullConfig(const FilePath& path, const AppConfig& config);
