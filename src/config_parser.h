#pragma once

#include <filesystem>
#include <string>

#include "config.h"

std::string LoadEmbeddedConfigTemplate();
bool ParseLayoutExpression(const std::string& text, LayoutNodeConfig& node);
AppConfig LoadConfig(const std::filesystem::path& path, bool includeOverlay = true);
