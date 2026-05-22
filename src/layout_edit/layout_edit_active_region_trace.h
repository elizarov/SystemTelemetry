#pragma once

#include "layout_model/dashboard_overlay_state.h"
#include "layout_model/layout_edit_active_region.h"
#include "util/trace.h"

struct AppConfig;

void WriteLayoutEditActiveRegionTrace(Trace& trace,
    const AppConfig& config,
    const LayoutEditActiveRegions& regions,
    const DashboardOverlayState& overlayState);
