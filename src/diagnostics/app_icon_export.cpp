#include "diagnostics/app_icon_export.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/file_path.h"
#include "util/utf8.h"
#include "widget/app_icon_geometry.h"

namespace {

constexpr std::uint32_t kCrcPolynomial = 0xEDB88320u;
constexpr std::uint32_t kAdlerModulus = 65521u;
constexpr std::size_t kMaxDeflateStoredBlockSize = 65535u;

bool SetError(std::string* errorText, std::string text) {
    if (errorText != nullptr) {
        *errorText = std::move(text);
    }
    return false;
}

bool DirectoryExists(const FilePath& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectoryExists(const FilePath& path, std::string* errorText) {
    if (path.empty() || DirectoryExists(path)) {
        return true;
    }

    const FilePath parent = path.ParentPath();
    if (!parent.empty() && parent.wstring() != path.wstring() && !EnsureDirectoryExists(parent, errorText)) {
        return false;
    }

    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    return SetError(errorText, "app_icon:create_directory_failed path=" + Utf8FromWide(path.wstring()));
}

void AppendByte(std::string& target, std::uint8_t value) {
    target.push_back(static_cast<char>(value));
}

void AppendBigEndian32(std::string& target, std::uint32_t value) {
    AppendByte(target, static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    AppendByte(target, static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    AppendByte(target, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    AppendByte(target, static_cast<std::uint8_t>(value & 0xFFu));
}

void AppendLittleEndian16(std::string& target, std::uint16_t value) {
    AppendByte(target, static_cast<std::uint8_t>(value & 0xFFu));
    AppendByte(target, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

std::uint32_t UpdateCrc32(std::uint32_t crc, std::string_view bytes) {
    for (const unsigned char value : bytes) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0u ? (crc >> 1) ^ kCrcPolynomial : crc >> 1;
        }
    }
    return crc;
}

std::uint32_t Crc32(std::string_view type, std::string_view data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    crc = UpdateCrc32(crc, type);
    crc = UpdateCrc32(crc, data);
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t Adler32(std::string_view bytes) {
    std::uint32_t a = 1u;
    std::uint32_t b = 0u;
    for (const unsigned char value : bytes) {
        a = (a + value) % kAdlerModulus;
        b = (b + a) % kAdlerModulus;
    }
    return (b << 16) | a;
}

void AppendPngChunk(std::string& png, std::string_view type, std::string_view data) {
    AppendBigEndian32(png, static_cast<std::uint32_t>(data.size()));
    png.append(type);
    png.append(data);
    AppendBigEndian32(png, Crc32(type, data));
}

std::string BuildRgbaScanlines(const AppIconBitmap& bitmap) {
    std::string scanlines;
    const std::size_t rowBytes = static_cast<std::size_t>(bitmap.size) * 4u;
    scanlines.reserve((rowBytes + 1u) * static_cast<std::size_t>(bitmap.size));
    for (int y = 0; y < bitmap.size; ++y) {
        AppendByte(scanlines, 0u);
        for (int x = 0; x < bitmap.size; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.size) + static_cast<std::size_t>(x)) *
                4u;
            AppendByte(scanlines, bitmap.bgra[offset + 2u]);
            AppendByte(scanlines, bitmap.bgra[offset + 1u]);
            AppendByte(scanlines, bitmap.bgra[offset + 0u]);
            AppendByte(scanlines, bitmap.bgra[offset + 3u]);
        }
    }
    return scanlines;
}

std::string BuildStoredDeflateZlibStream(std::string_view bytes) {
    std::string stream;
    stream.reserve(bytes.size() + (bytes.size() / kMaxDeflateStoredBlockSize + 1u) * 5u + 6u);
    AppendByte(stream, 0x78u);
    AppendByte(stream, 0x01u);

    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const std::size_t blockSize = (std::min)(remaining, kMaxDeflateStoredBlockSize);
        const bool finalBlock = offset + blockSize == bytes.size();
        AppendByte(stream, finalBlock ? 0x01u : 0x00u);
        const auto length = static_cast<std::uint16_t>(blockSize);
        AppendLittleEndian16(stream, length);
        AppendLittleEndian16(stream, static_cast<std::uint16_t>(~length));
        stream.append(bytes.substr(offset, blockSize));
        offset += blockSize;
    }

    AppendBigEndian32(stream, Adler32(bytes));
    return stream;
}

std::string BuildPng(const AppIconBitmap& bitmap) {
    std::string ihdr;
    ihdr.reserve(13u);
    AppendBigEndian32(ihdr, static_cast<std::uint32_t>(bitmap.size));
    AppendBigEndian32(ihdr, static_cast<std::uint32_t>(bitmap.size));
    AppendByte(ihdr, 8u);
    AppendByte(ihdr, 6u);
    AppendByte(ihdr, 0u);
    AppendByte(ihdr, 0u);
    AppendByte(ihdr, 0u);

    const std::string scanlines = BuildRgbaScanlines(bitmap);
    const std::string idat = BuildStoredDeflateZlibStream(scanlines);

    std::string png;
    png.reserve(8u + 12u + ihdr.size() + 12u + idat.size() + 12u);
    png.append("\x89PNG\r\n\x1A\n", 8u);
    AppendPngChunk(png, "IHDR", ihdr);
    AppendPngChunk(png, "IDAT", idat);
    AppendPngChunk(png, "IEND", {});
    return png;
}

bool SaveBitmapAsPng(const AppIconBitmap& bitmap, const FilePath& imagePath, std::string* errorText) {
    const std::size_t expectedBytes =
        static_cast<std::size_t>(bitmap.size) * static_cast<std::size_t>(bitmap.size) * 4u;
    if (bitmap.size <= 0 || bitmap.bgra.size() != expectedBytes) {
        return SetError(errorText, "app_icon:invalid_bitmap");
    }
    if (!EnsureDirectoryExists(imagePath.ParentPath(), errorText)) {
        return false;
    }

    const std::string png = BuildPng(bitmap);
    if (!WriteFileBinary(imagePath, png)) {
        return SetError(errorText, "app_icon:write_failed path=" + Utf8FromWide(imagePath.wstring()));
    }
    return true;
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
    return SaveBitmapAsPng(bitmap, imagePath, errorText);
}
