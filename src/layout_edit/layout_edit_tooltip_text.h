#pragma once

#include <string>

#include "config/config.h"
#include "widget/layout_edit_types.h"

const char* LayoutEditTooltipPayloadTraceKind(const TooltipPayload& payload);
bool BuildLayoutEditTooltipTextForPayload(const AppConfig& config,
    const TooltipPayload& payload,
    std::string& tooltipText,
    std::string* errorReason = nullptr);
