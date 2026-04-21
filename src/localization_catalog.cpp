#include "localization_catalog.h"

#include <algorithm>
#include <windows.h>

#include "resource.h"

#include "util/utf8.h"

namespace {

LocalizationCatalogMap g_localizationCatalog;
bool g_localizationCatalogInitialized = false;

std::string Trim(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    return std::string(first, last);
}

std::string LoadUtf8Resource(WORD resourceId, const wchar_t* resourceType) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return {};
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), resourceType);
    if (resource == nullptr) {
        return {};
    }

    HGLOBAL loadedResource = LoadResource(module, resource);
    if (loadedResource == nullptr) {
        return {};
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0) {
        return {};
    }

    const void* resourceData = LockResource(loadedResource);
    if (resourceData == nullptr) {
        return {};
    }

    std::string text(static_cast<const char*>(resourceData), static_cast<size_t>(resourceSize));
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!IsValidUtf8(text)) {
        return {};
    }
    return text;
}

}  // namespace

LocalizationCatalogMap ParseLocalizationCatalog(std::string_view text) {
    LocalizationCatalogMap values;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find_first_of("\r\n", lineStart);
        if (lineEnd == std::string_view::npos) {
            lineEnd = text.size();
        }

        std::string line = Trim(std::string(text.substr(lineStart, lineEnd - lineStart)));
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

    g_localizationCatalog = ParseLocalizationCatalog(LoadUtf8Resource(IDR_LOCALIZATION_CATALOG, RT_RCDATA));
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
