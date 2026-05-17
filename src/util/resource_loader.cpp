#include "util/resource_loader.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "resource.h"

namespace {

constexpr uint32_t kCompressedResourceMagic = 0x5A4C4443u;
constexpr size_t kCompressedResourceHeaderSize = 8;
constexpr size_t kTextAtlasHeaderSize = 8;
constexpr int kMinMatchLength = 3;
constexpr size_t kExtendedMatchLength = kMinMatchLength + 15;
constexpr size_t kTextResourceCount = static_cast<size_t>(TextResourceId::Count);

struct TextAtlas {
    std::string storage;
    std::array<size_t, kTextResourceCount + 1> offsets = {};
    bool valid = false;
};

uint32_t ReadLittleEndianUint32(const char* data) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[0])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[3])) << 24);
}

std::string DecompressResourceData(std::string_view data) {
    const uint32_t decompressedSize = ReadLittleEndianUint32(data.data() + sizeof(uint32_t));
    std::string output;
    output.reserve(decompressedSize);

    size_t input = kCompressedResourceHeaderSize;
    while (input < data.size() && output.size() < decompressedSize) {
        const unsigned char flags = static_cast<unsigned char>(data[input++]);
        for (int bit = 0; bit < 8 && output.size() < decompressedSize; ++bit) {
            if ((flags & (1u << bit)) == 0) {
                if (input >= data.size()) {
                    return {};
                }
                output.push_back(data[input++]);
                continue;
            }
            if (input + 1 >= data.size()) {
                return {};
            }
            const uint16_t token = static_cast<uint16_t>(static_cast<unsigned char>(data[input])) |
                                   (static_cast<uint16_t>(static_cast<unsigned char>(data[input + 1])) << 8);
            input += 2;
            const size_t offset = static_cast<size_t>(token >> 4) + 1;
            size_t length = static_cast<size_t>(token & 0x0F) + kMinMatchLength;
            // Length code 15 carries one extra byte for 18-byte and longer matches.
            if (length == kExtendedMatchLength) {
                if (input >= data.size()) {
                    return {};
                }
                length += static_cast<unsigned char>(data[input++]);
            }
            if (offset > output.size() || output.size() + length > decompressedSize) {
                return {};
            }
            for (size_t index = 0; index < length; ++index) {
                output.push_back(output[output.size() - offset]);
            }
        }
    }
    return output.size() == decompressedSize ? output : std::string{};
}

std::string LoadCompressedResourceData(int resourceId) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return {};
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
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

    const std::string_view compressedData(static_cast<const char*>(resourceData), static_cast<size_t>(resourceSize));
    if (compressedData.size() < kCompressedResourceHeaderSize ||
        ReadLittleEndianUint32(compressedData.data()) != kCompressedResourceMagic) {
        return {};
    }
    return DecompressResourceData(compressedData);
}

TextAtlas LoadTextAtlas() {
    TextAtlas atlas;
    atlas.storage = LoadCompressedResourceData(IDR_TEXT_RESOURCE_ATLAS);
    if (atlas.storage.size() < kTextAtlasHeaderSize) {
        atlas.storage.clear();
        return atlas;
    }

    const size_t configLength = ReadLittleEndianUint32(atlas.storage.data());
    const size_t localizationLength = ReadLittleEndianUint32(atlas.storage.data() + sizeof(uint32_t));
    if (atlas.storage.size() - kTextAtlasHeaderSize < configLength) {
        atlas.storage.clear();
        return atlas;
    }
    const size_t localizationOffset = kTextAtlasHeaderSize + configLength;
    if (atlas.storage.size() - localizationOffset < localizationLength) {
        atlas.storage.clear();
        return atlas;
    }

    atlas.offsets[static_cast<size_t>(TextResourceId::ConfigTemplate)] = kTextAtlasHeaderSize;
    atlas.offsets[static_cast<size_t>(TextResourceId::LocalizationCatalog)] = localizationOffset;
    atlas.offsets[static_cast<size_t>(TextResourceId::ResourceStringCatalog)] = localizationOffset + localizationLength;
    atlas.offsets[kTextResourceCount] = atlas.storage.size();
    atlas.valid = true;
    return atlas;
}

const TextAtlas& GetTextAtlas() {
    static const TextAtlas atlas = LoadTextAtlas();
    return atlas;
}

}  // namespace

std::string LoadUtf8ResourceData(TextResourceId resourceId) {
    const size_t resourceIndex = static_cast<size_t>(resourceId);
    if (resourceIndex >= kTextResourceCount) {
        return {};
    }

    const TextAtlas& atlas = GetTextAtlas();
    if (!atlas.valid) {
        return {};
    }

    const size_t offset = atlas.offsets[resourceIndex];
    const size_t length = atlas.offsets[resourceIndex + 1] - offset;
    std::string text(atlas.storage.data() + offset, length);
    return text;
}
