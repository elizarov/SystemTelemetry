#pragma once

#include <memory>

#include "diagnostics/diagnostics.h"

struct LayoutGuideSheetConfig;

struct HeadlessLayoutGuideSheetOutputContext {
    HeadlessLayoutGuideSheetOutputContext();
    ~HeadlessLayoutGuideSheetOutputContext();

    HeadlessLayoutGuideSheetOutputContext(const HeadlessLayoutGuideSheetOutputContext&) = delete;
    HeadlessLayoutGuideSheetOutputContext& operator=(const HeadlessLayoutGuideSheetOutputContext&) = delete;

    std::unique_ptr<LayoutGuideSheetConfig> guideSheet;
};

bool InitializeHeadlessLayoutGuideSheetOutput(
    HeadlessLayoutGuideSheetOutputContext& context, std::string* errorText = nullptr);
DiagnosticsOutputHandlers CreateHeadlessDiagnosticsOutputHandlers(HeadlessLayoutGuideSheetOutputContext& context);
