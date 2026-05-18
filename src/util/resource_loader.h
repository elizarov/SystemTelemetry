#pragma once

#include <string>

enum class TextResourceId {
    ConfigTemplate,
    LocalizationCatalog,
    ResourceStringCatalog,
    Count,
};

std::string LoadUtf8ResourceData(TextResourceId resourceId);
