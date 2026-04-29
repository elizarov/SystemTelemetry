#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

using LocalizationCatalogMap = std::unordered_map<std::string, std::string>;

LocalizationCatalogMap ParseLocalizationCatalog(std::string_view text);
void InitializeLocalizationCatalog();
void ReplaceLocalizationCatalogForTesting(LocalizationCatalogMap catalog);
const LocalizationCatalogMap& LocalizationCatalog();
std::string FindLocalizedText(std::string_view key);
