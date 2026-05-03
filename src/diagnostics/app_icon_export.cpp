#include "diagnostics/app_icon_export.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

#include "util/utf8.h"
#include "widget/app_icon_geometry.h"

namespace {

void AppendBigEndian32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

std::uint32_t Crc32(const std::uint8_t* data, size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t Adler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void AppendChunk(std::vector<std::uint8_t>& png, const char type[4], const std::vector<std::uint8_t>& data) {
    AppendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    const size_t chunkStart = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    AppendBigEndian32(png, Crc32(png.data() + chunkStart, png.size() - chunkStart));
}

std::vector<std::uint8_t> DeflateStored(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> output;
    output.reserve(data.size() + (data.size() / 65535u + 1u) * 5u + 6u);
    output.push_back(0x78);
    output.push_back(0x01);
    size_t offset = 0;
    while (offset < data.size()) {
        const size_t remaining = data.size() - offset;
        const std::uint16_t blockSize = static_cast<std::uint16_t>(std::min<size_t>(remaining, 65535u));
        const bool finalBlock = offset + blockSize >= data.size();
        output.push_back(finalBlock ? 0x01 : 0x00);
        output.push_back(static_cast<std::uint8_t>(blockSize & 0xFFu));
        output.push_back(static_cast<std::uint8_t>((blockSize >> 8) & 0xFFu));
        const std::uint16_t inverse = static_cast<std::uint16_t>(~blockSize);
        output.push_back(static_cast<std::uint8_t>(inverse & 0xFFu));
        output.push_back(static_cast<std::uint8_t>((inverse >> 8) & 0xFFu));
        output.insert(output.end(),
            data.begin() + static_cast<std::ptrdiff_t>(offset),
            data.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    }
    AppendBigEndian32(output, Adler32(data));
    return output;
}

std::vector<std::uint8_t> EncodePngRgba(const AppIconBitmap& bitmap) {
    std::vector<std::uint8_t> scanlines;
    scanlines.reserve(static_cast<size_t>(bitmap.size) * (static_cast<size_t>(bitmap.size) * 4u + 1u));
    for (int y = 0; y < bitmap.size; ++y) {
        scanlines.push_back(0);
        for (int x = 0; x < bitmap.size; ++x) {
            const size_t offset =
                (static_cast<size_t>(y) * static_cast<size_t>(bitmap.size) + static_cast<size_t>(x)) * 4u;
            scanlines.push_back(bitmap.bgra[offset + 2u]);
            scanlines.push_back(bitmap.bgra[offset + 1u]);
            scanlines.push_back(bitmap.bgra[offset + 0u]);
            scanlines.push_back(bitmap.bgra[offset + 3u]);
        }
    }

    std::vector<std::uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    AppendBigEndian32(ihdr, static_cast<std::uint32_t>(bitmap.size));
    AppendBigEndian32(ihdr, static_cast<std::uint32_t>(bitmap.size));
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    AppendChunk(png, "IHDR", ihdr);
    AppendChunk(png, "IDAT", DeflateStored(scanlines));
    AppendChunk(png, "IEND", {});
    return png;
}

}  // namespace

bool SaveAppIconPng(const FilePath& imagePath, const AppConfig& config, int size, std::string* errorText) {
    if (!IsValidAppIconSize(size)) {
        if (errorText != nullptr) {
            *errorText = "app_icon:invalid_size";
        }
        return false;
    }

    const AppIconBitmap bitmap = RenderAppIconBitmap(config, size);
    const std::vector<std::uint8_t> png = EncodePngRgba(bitmap);
    std::ofstream output(imagePath.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        if (errorText != nullptr) {
            *errorText = "app_icon:file_open_failed path=\"" + Utf8FromWide(imagePath.wstring()) + "\"";
        }
        return false;
    }
    output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!output) {
        if (errorText != nullptr) {
            *errorText = "app_icon:file_write_failed path=\"" + Utf8FromWide(imagePath.wstring()) + "\"";
        }
        return false;
    }
    return true;
}
