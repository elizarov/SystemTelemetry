#pragma once

#include <string>

enum class BoardVendor {
    Unknown,
    Gigabyte,
    Msi,
};

struct BoardVendorInfo {
    std::string manufacturer;
    std::string product;
};

const char* BoardVendorName(BoardVendor vendor);
BoardVendor SelectBoardVendor(const BoardVendorInfo& info);
