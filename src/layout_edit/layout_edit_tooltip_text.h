#pragma once

#include <string>

#include "widget/layout_edit_types.h"

struct AppConfig;

const char* LayoutEditTooltipPayloadTraceKind(const TooltipPayload& payload);
bool BuildLayoutEditTooltipTextForPayload(const AppConfig& config,
    const TooltipPayload& payload,
    std::string& tooltipText,
    std::string* errorReason = nullptr);
