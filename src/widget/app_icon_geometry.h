#pragma once

#include <cstdint>
#include <vector>

struct AppConfig;

inline constexpr int kDefaultAppIconSize = 256;
inline constexpr int kMinAppIconSize = 16;
inline constexpr int kMaxAppIconSize = 1024;

struct AppIconBitmap {
    int size = 0;
    std::vector<std::uint8_t> bgra;
};

bool IsValidAppIconSize(int size);
AppIconBitmap RenderAppIconBitmap(const AppConfig& config, int size);
