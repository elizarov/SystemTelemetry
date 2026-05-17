#pragma once

#include <string>

enum class TextResourceId {
    ConfigTemplate,
    LocalizationCatalog,
    ResourceStringCatalog,
};

std::string LoadUtf8ResourceData(TextResourceId resourceId);
