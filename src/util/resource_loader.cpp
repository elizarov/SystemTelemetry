#include "util/resource_loader.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "util/utf8.h"

namespace {

constexpr unsigned char kCompressedResourceMagic[] = {'C', 'D', 'L', 'Z'};
constexpr size_t kCompressedResourceHeaderSize = 8;
constexpr int kMinMatchLength = 3;

uint32_t ReadLittleEndianUint32(const char* data) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[0])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[3])) << 24);
}

bool IsCompressedResourceData(const std::string& data) {
    return data.size() >= kCompressedResourceHeaderSize &&
           std::memcmp(data.data(), kCompressedResourceMagic, sizeof(kCompressedResourceMagic)) == 0;
}

std::string DecompressResourceData(const std::string& data) {
    const uint32_t decompressedSize = ReadLittleEndianUint32(data.data() + sizeof(kCompressedResourceMagic));
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
            const size_t offset = static_cast<size_t>((token >> 4) + 1);
            const size_t length = static_cast<size_t>((token & 0x0F) + kMinMatchLength);
            if (offset == 0 || offset > output.size() || output.size() + length > decompressedSize) {
                return {};
            }
            for (size_t index = 0; index < length; ++index) {
                output.push_back(output[output.size() - offset]);
            }
        }
    }
    return output.size() == decompressedSize ? output : std::string{};
}

}  // namespace

std::string LoadUtf8ResourceData(int resourceId) {
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

    std::string text(static_cast<const char*>(resourceData), static_cast<size_t>(resourceSize));
    if (IsCompressedResourceData(text)) {
        text = DecompressResourceData(text);
        if (text.empty()) {
            return {};
        }
    }
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    if (!IsValidUtf8(text)) {
        return {};
    }
    return text;
}
