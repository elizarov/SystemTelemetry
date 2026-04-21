#pragma once

#include <optional>
#include <string>

#include "config.h"
#include "layout_edit_types.h"

const char* LayoutEditTooltipPayloadTraceKind(const TooltipPayload& payload);
std::optional<std::wstring> BuildLayoutEditTooltipTextForPayload(
    const AppConfig& config, const TooltipPayload& payload, std::string* errorReason = nullptr);
