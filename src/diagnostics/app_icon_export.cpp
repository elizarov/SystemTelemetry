#include "diagnostics/app_icon_export.h"

#include <windows.h>

#include <string>
#include <utility>

#include "renderer/png_export.h"
#include "util/file_path.h"
#include "widget/app_icon_geometry.h"

namespace {

bool SetError(std::string* errorText, std::string text) {
    if (errorText != nullptr) {
        *errorText = std::move(text);
    }
    return false;
}

bool DirectoryExists(const FilePath& path) {
    const std::wstring widePath = path.Wide();
    const DWORD attributes = GetFileAttributesW(widePath.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectoryExists(const FilePath& path, std::string* errorText) {
    if (path.empty() || DirectoryExists(path)) {
        return true;
    }

    const FilePath parent = path.ParentPath();
    if (!parent.empty() && parent.string() != path.string() && !EnsureDirectoryExists(parent, errorText)) {
        return false;
    }

    const std::wstring widePath = path.Wide();
    if (CreateDirectoryW(widePath.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    return SetError(errorText, "app_icon:create_directory_failed path=" + path.string());
}

bool SaveBitmapAsPng(const AppIconBitmap& bitmap, const FilePath& imagePath, std::string* errorText) {
    if (bitmap.size <= 0) {
        return SetError(errorText, "app_icon:invalid_bitmap");
    }
    if (!EnsureDirectoryExists(imagePath.ParentPath(), errorText)) {
        return false;
    }

    return SaveBgraPng(imagePath, bitmap.size, bitmap.size, bitmap.bgra, errorText);
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
