#pragma once

#include <string>

enum class TextResourceId {
    ConfigTemplate,
    LocalizationCatalog,
    ResourceStringCatalog,
    Count,
};

std::string LoadTextResourceData(TextResourceId resourceId);
