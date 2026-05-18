#include "util/localization_catalog.h"

#include <cstdint>
#include <utility>

#include "util/resource_loader.h"
#include "util/resource_strings.h"
#include "util/strings.h"
#include "util/text_format.h"

namespace {

LocalizationCatalogMap g_localizationCatalog;
bool g_localizationCatalogInitialized = false;

}  // namespace

LocalizationCatalogMap ParseLocalizationCatalog(std::string_view text) {
    LocalizationCatalogMap values;
    std::string section;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }

        std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
        if (!line.empty() && line[0] != '#' && line[0] != ';') {
            if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                section = Trim(std::string_view(line).substr(1, line.size() - 2));
            } else if (const size_t equals = line.find('='); equals != std::string::npos) {
                const std::string key = Trim(line.substr(0, equals));
                const std::string value = Trim(line.substr(equals + 1));
                if (!key.empty()) {
                    values[section.empty() ? key : FormatText("%s.%s", section.c_str(), key.c_str())] = value;
                }
            }
        }

        lineStart = lineEnd;
        while (lineStart < text.size() && (text[lineStart] == '\r' || text[lineStart] == '\n')) {
            ++lineStart;
        }
    }
    return values;
}

void InitializeLocalizationCatalog() {
    if (g_localizationCatalogInitialized) {
        return;
    }

    g_localizationCatalog = ParseLocalizationCatalog(LoadUtf8ResourceData(TextResourceId::LocalizationCatalog));
    g_localizationCatalogInitialized = true;
}

void ReplaceLocalizationCatalogForTesting(LocalizationCatalogMap catalog) {
    g_localizationCatalog = std::move(catalog);
    g_localizationCatalogInitialized = true;
}

const LocalizationCatalogMap& LocalizationCatalog() {
    return g_localizationCatalog;
}

std::string FindLocalizedText(std::string_view key) {
    const LocalizationCatalogMap& catalog = LocalizationCatalog();
    const auto it = catalog.find(std::string(key));
    return it != catalog.end() ? it->second : std::string{};
}

const char* FindLocalizedText(ResourceStringId key) {
    const char* textKey = ResourceStringText(key);
    const LocalizationCatalogMap& catalog = LocalizationCatalog();
    if (textKey[0] != '\0') {
        const auto it = catalog.find(textKey);
        return it != catalog.end() ? it->second.c_str() : textKey;
    }
    const auto target = static_cast<std::uint32_t>(key);
    for (const auto& [catalogKey, value] : catalog) {
        if (resource_strings_detail::ResourceStringHash(catalogKey.data(), catalogKey.size()) == target) {
            return value.c_str();
        }
    }
    return textKey;
}
