#pragma once

#include <string>
#include <string_view>

#include "config/config.h"
#include "config/metric_catalog.h"
#include "util/file_path.h"

std::string LoadEmbeddedConfigTemplate();
bool ParseLayoutExpression(const std::string& text, LayoutNodeConfig& node);
AppConfig LoadConfig(const FilePath& path, bool includeOverlay = true, const ConfigParseContext& context = {});
AppConfig LoadConfigWithExtraTemplate(
    const FilePath& path, bool includeOverlay, const ConfigParseContext& context, std::string_view extraTemplate);
