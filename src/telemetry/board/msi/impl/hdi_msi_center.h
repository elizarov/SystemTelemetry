#pragma once

#include <memory>
#include <optional>

#include "telemetry/board/msi/board_msi_center_bridge.h"
#include "util/file_path.h"

class Trace;

class MsiCenterHdi {
public:
    virtual ~MsiCenterHdi() = default;

    virtual std::optional<FilePath> FindInstalledDirectory() = 0;
    virtual bool Capture(const char* msiCenterDirectory, MsiCenterCaptureSink& sink) = 0;
};

std::unique_ptr<MsiCenterHdi> CreateProductionMsiCenterHdi(Trace& trace);
