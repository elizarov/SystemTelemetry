#include "util/resource_loader.h"

#include <windows.h>

#include <cstdint>
#include <string_view>

#include "resource.h"

namespace {

constexpr uint32_t kCompressedResourceMagic = 0x5A4C4443u;
constexpr size_t kCompressedResourceHeaderSize = 8;
constexpr size_t kTextAtlasHeaderSize = 8;
constexpr int kMinMatchLength = 3;

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
            const size_t length = static_cast<size_t>(token & 0x0F) + kMinMatchLength;
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

}  // namespace

std::string LoadUtf8ResourceData(TextResourceId resourceId) {
    if (resourceId != TextResourceId::ConfigTemplate && resourceId != TextResourceId::LocalizationCatalog &&
        resourceId != TextResourceId::ResourceStringCatalog) {
        return {};
    }

    std::string atlas = LoadCompressedResourceData(IDR_TEXT_RESOURCE_ATLAS);
    if (atlas.size() < kTextAtlasHeaderSize) {
        return {};
    }

    const size_t configLength = ReadLittleEndianUint32(atlas.data());
    const size_t localizationLength = ReadLittleEndianUint32(atlas.data() + sizeof(uint32_t));
    if (atlas.size() - kTextAtlasHeaderSize < configLength) {
        return {};
    }
    const size_t localizationOffset = kTextAtlasHeaderSize + configLength;
    if (atlas.size() - localizationOffset < localizationLength) {
        return {};
    }

    size_t offset = kTextAtlasHeaderSize;
    size_t length = configLength;
    if (resourceId == TextResourceId::LocalizationCatalog) {
        offset = localizationOffset;
        length = localizationLength;
    } else if (resourceId == TextResourceId::ResourceStringCatalog) {
        offset = localizationOffset + localizationLength;
        length = atlas.size() - offset;
    }
    std::string text(atlas.data() + offset, length);
    return text;
}
