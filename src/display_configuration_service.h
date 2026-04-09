#pragma once

#include <memory>
#include <ostream>

#include "app_diagnostics.h"
#include "app_monitor.h"
#include "config.h"
#include "snapshot_dump.h"

class DisplayConfigurationService {
public:
    bool ApplyConfiguredWallpaper(const AppConfig& config, std::ostream* traceStream) const;
    std::vector<DisplayMenuOption> EnumerateDisplayOptions(const AppConfig& config) const;
    std::optional<TargetMonitorInfo> FindTargetMonitor(const std::string& requestedName) const;
    bool ConfigureDisplay(const AppConfig& config, const TelemetryDump& dump, UINT targetDpi,
        std::ostream* traceStream, HWND owner) const;
};
