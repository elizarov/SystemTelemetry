#pragma once

#include <memory>

#include "telemetry/board/board_vendor_selection.h"

class Trace;

class BoardDiscoveryHdi {
public:
    virtual ~BoardDiscoveryHdi() = default;

    virtual BoardVendorInfo ReadBoardVendorInfo() = 0;
};

std::unique_ptr<BoardDiscoveryHdi> CreateProductionBoardDiscoveryHdi();
