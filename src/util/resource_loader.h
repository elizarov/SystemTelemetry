#pragma once

#include <string>

enum class TextResourceId {
    ConfigTemplate,
    LocalizationCatalog,
};

std::string LoadUtf8ResourceData(TextResourceId resourceId);
