#include "telemetry/board/board_vendor_selection.h"

#include "util/strings.h"

const char* BoardVendorName(BoardVendor vendor) {
    switch (vendor) {
        case BoardVendor::Gigabyte:
            return "Gigabyte";
        case BoardVendor::Msi:
            return "MSI";
        case BoardVendor::Unknown:
        default:
            return "Unknown";
    }
}

BoardVendor SelectBoardVendor(const BoardVendorInfo& info) {
    if (ContainsInsensitive(info.manufacturer, "micro-star") || ContainsInsensitive(info.manufacturer, "msi")) {
        return BoardVendor::Msi;
    }
    if (ContainsInsensitive(info.manufacturer, "gigabyte")) {
        return BoardVendor::Gigabyte;
    }
    return BoardVendor::Unknown;
}
