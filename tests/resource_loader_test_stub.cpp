#include <fstream>
#include <sstream>

#include "util/file_path.h"
#include "util/resource_loader.h"

std::string LoadTextResourceData(TextResourceId resourceId) {
    // Unit tests need RES_STR text without embedding the full app config/localization atlas.
    if (resourceId != TextResourceId::ResourceStringCatalog) {
        return {};
    }

    const FilePath path = FilePath(CASEDASH_SOURCE_DIR) / "build" / "cmake" / "generated" / "compressed_resources" /
                          "resource_strings.txt";
    std::ifstream input(path.string(), std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}
