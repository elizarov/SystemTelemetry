#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "config/config.h"
#include "config/config_runtime_fields.h"
#include "util/function_ref.h"

struct AppConfig;

using ConfigColorLookup = FunctionRef<std::optional<ColorConfig>(std::string_view)>;

const ThemeConfig* ResolveConfiguredTheme(LayoutConfig& layout, std::string& themeName);
std::optional<ColorConfig> FindThemeColorToken(const ThemeConfig& theme, std::string_view name);
std::optional<ColorConfig> FindConfigColorFieldByKey(
    std::span<const RuntimeConfigFieldDescriptor> fields, const void* owner, std::string_view name);
void ResolveConfigColorFieldsInPlace(
    std::span<const RuntimeConfigFieldDescriptor> fields, void* owner, ConfigColorLookup lookup);
void ResolveConfiguredColors(AppConfig& config);
