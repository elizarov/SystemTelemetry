#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/file_path.h"

struct IWICBitmapSource;
struct IWICImagingFactory;

enum class PngPixelFormat {
    BgrOpaque,
    BgraWithAlpha,
};

bool SaveBgraPng(
    const FilePath& imagePath, int width, int height, const std::vector<std::uint8_t>& bgra, std::string* errorText);
bool SaveWicBitmapSourcePng(
    IWICImagingFactory* factory,
    IWICBitmapSource* source,
    const FilePath& imagePath,
    PngPixelFormat pixelFormat,
    std::string_view errorPrefix,
    std::string* errorText);
