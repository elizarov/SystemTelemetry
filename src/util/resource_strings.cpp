#include "util/resource_strings.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "util/resource_loader.h"

namespace {

struct ResourceStringLookupEntry {
    std::uint32_t id = 0;
    const char* text = nullptr;
};

struct ResourceStringCatalog {
    std::string storage;
    std::vector<ResourceStringLookupEntry> lookup;
};

std::size_t ResourceStringLookupSize(std::size_t count) {
    std::size_t size = 1;
    while (size < count * 2) {
        size *= 2;
    }
    return size;
}

void AddResourceString(std::vector<ResourceStringLookupEntry>& strings, const char* text, std::size_t length) {
    strings.push_back({resource_strings_detail::ResourceStringHash(text, length), text});
}

std::vector<ResourceStringLookupEntry> BuildResourceStringLookup(
    const std::vector<ResourceStringLookupEntry>& strings) {
    std::vector<ResourceStringLookupEntry> lookup(ResourceStringLookupSize(strings.size()));
    const std::size_t mask = lookup.size() - 1;
    for (const ResourceStringLookupEntry& string : strings) {
        std::size_t slot = static_cast<std::size_t>(string.id) & mask;
        // Generated ids are collision-checked; probing here only resolves table slot collisions.
        while (lookup[slot].text != nullptr) {
            slot = (slot + 1) & mask;
        }
        lookup[slot] = {string.id, string.text};
    }
    return lookup;
}

ResourceStringCatalog LoadResourceStringCatalog() {
    ResourceStringCatalog catalog;
    std::vector<ResourceStringLookupEntry> strings;
    catalog.storage = LoadUtf8ResourceData(TextResourceId::ResourceStringCatalog);
    const char* start = catalog.storage.data();
    for (char& ch : catalog.storage) {
        if (ch != '\n') {
            continue;
        }
        const std::size_t length = static_cast<std::size_t>(&ch - start);
        ch = '\0';
        AddResourceString(strings, start, length);
        start = &ch + 1;
    }
    if (start != catalog.storage.data() + catalog.storage.size()) {
        AddResourceString(
            strings, start, static_cast<std::size_t>(catalog.storage.data() + catalog.storage.size() - start));
    }
    catalog.lookup = BuildResourceStringLookup(strings);
    return catalog;
}

const ResourceStringCatalog& GetResourceStringCatalog() {
    static ResourceStringCatalog catalog = LoadResourceStringCatalog();
    return catalog;
}

}  // namespace

const char* ResourceStringText(ResourceStringId id) {
    const ResourceStringCatalog& catalog = GetResourceStringCatalog();
    if (catalog.lookup.empty()) {
        return "";
    }
    const std::uint32_t target = static_cast<std::uint32_t>(id);
    const std::size_t mask = catalog.lookup.size() - 1;
    std::size_t slot = static_cast<std::size_t>(target) & mask;
    while (catalog.lookup[slot].text != nullptr) {
        const ResourceStringLookupEntry& entry = catalog.lookup[slot];
        if (entry.id == target) {
            return entry.text;
        }
        slot = (slot + 1) & mask;
    }
    return "";
}
