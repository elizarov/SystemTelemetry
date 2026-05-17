#include "util/resource_strings.h"

#include <cstddef>
#include <string>
#include <vector>

#include "util/resource_loader.h"

namespace {

struct ResourceStringCatalog {
    std::string storage;
    std::vector<const char*> strings;
};

ResourceStringCatalog LoadResourceStringCatalog() {
    ResourceStringCatalog catalog;
    catalog.storage = LoadUtf8ResourceData(TextResourceId::ResourceStringCatalog);
    const char* start = catalog.storage.data();
    for (char& ch : catalog.storage) {
        if (ch != '\n') {
            continue;
        }
        ch = '\0';
        catalog.strings.push_back(start);
        start = &ch + 1;
    }
    if (start != catalog.storage.data() + catalog.storage.size()) {
        catalog.strings.push_back(start);
    }
    return catalog;
}

const ResourceStringCatalog& GetResourceStringCatalog() {
    static ResourceStringCatalog catalog = LoadResourceStringCatalog();
    return catalog;
}

}  // namespace

const char* ResourceStringText(ResourceStringId id) {
    const ResourceStringCatalog& catalog = GetResourceStringCatalog();
    const std::size_t index = static_cast<std::uint32_t>(id);
    if (index >= catalog.strings.size()) {
        return "";
    }
    return catalog.strings[index];
}
