#include "headless/layout_guide_sheet_output.h"

#include <windows.h>

#include "diagnostics/constants.h"
#include "layout_guide_sheet/layout_guide_sheet.h"
#include "resource.h"
#include "util/paths.h"
#include "util/resource_strings.h"
#include "util/text_format.h"

namespace {

bool LoadHeadlessLayoutGuideSheetConfig(std::string* configText, std::string* errorText) {
    if (configText != nullptr) {
        configText->clear();
    }
    if (errorText != nullptr) {
        errorText->clear();
    }

    HMODULE module = GetModuleHandleA(nullptr);
    HRSRC resource = FindResourceA(module, MAKEINTRESOURCEA(IDR_LAYOUT_GUIDE_SHEET_CONFIG), RT_RCDATA);
    if (resource == nullptr) {
        if (errorText != nullptr) {
            *errorText = ResourceStringText(RES_STR("layout_guide_sheet_config_resource_missing"));
        }
        return false;
    }

    HGLOBAL loadedResource = LoadResource(module, resource);
    const DWORD resourceSize = SizeofResource(module, resource);
    const void* data = LockResource(loadedResource);
    if (loadedResource == nullptr || data == nullptr || resourceSize == 0) {
        if (errorText != nullptr) {
            *errorText = ResourceStringText(RES_STR("layout_guide_sheet_config_resource_empty"));
        }
        return false;
    }

    if (configText != nullptr) {
        configText->assign(static_cast<const char*>(data), static_cast<size_t>(resourceSize));
    }
    return true;
}

bool WriteHeadlessDiagnosticsExtraOutputs(const DiagnosticsOptions& options,
    const TelemetryDump& dump,
    const AppConfig& config,
    double scale,
    Trace& trace,
    std::string* errorText) {
    if (!options.layoutGuideSheet) {
        return true;
    }

    const FilePath imagePath = ResolveDiagnosticsOutputPath(
        GetWorkingDirectory(), options.layoutGuideSheetPath, kDefaultLayoutGuideSheetFileName);
    std::string saveError;
    if (SaveLayoutGuideSheetPng(imagePath, dump.snapshot, config, scale, trace, &saveError)) {
        return true;
    }

    if (errorText != nullptr) {
        AssignFormat(*errorText, "Failed to save layout guide sheet:\n%s", imagePath.string().c_str());
        if (!saveError.empty()) {
            AppendFormat(*errorText, "\n\n%s", saveError.c_str());
        }
    }
    return false;
}

}  // namespace

DiagnosticsOutputHandlers CreateHeadlessDiagnosticsOutputHandlers() {
    DiagnosticsOutputHandlers handlers;
    handlers.writeExtraOutputs = &WriteHeadlessDiagnosticsExtraOutputs;
    handlers.loadExtraConfig = &LoadHeadlessLayoutGuideSheetConfig;
    handlers.resolveExtraConfig = &ResolveLayoutGuideSheetColors;
    return handlers;
}
