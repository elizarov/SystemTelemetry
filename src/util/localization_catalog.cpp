#include "util/localization_catalog.h"

#include "resource.h"
#include "util/resource_loader.h"
#include "util/strings.h"

namespace {

LocalizationCatalogMap g_localizationCatalog;
bool g_localizationCatalogInitialized = false;

}  // namespace

LocalizationCatalogMap ParseLocalizationCatalog(std::string_view text) {
    LocalizationCatalogMap values;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }

        std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
        if (!line.empty() && line[0] != '#' && line[0] != ';') {
            const size_t equals = line.find('=');
            if (equals != std::string::npos) {
                const std::string key = Trim(line.substr(0, equals));
                const std::string value = Trim(line.substr(equals + 1));
                if (!key.empty()) {
                    values[key] = value;
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

    g_localizationCatalog = ParseLocalizationCatalog(LoadUtf8ResourceData(IDR_LOCALIZATION_CATALOG));
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
