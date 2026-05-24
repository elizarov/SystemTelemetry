#include <windows.h>

#include <cstdio>
#include <string_view>

#include "diagnostics/crash_report.h"
#include "diagnostics/diagnostics.h"
#include "headless/layout_guide_sheet_output.h"
#include "util/command_line.h"

namespace {

void ReportHeadlessDiagnosticsError(const DiagnosticsOptions&, std::string_view message) {
    std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()), message.data());
}

}  // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const CommandLineArguments commandLine = GetCommandLineArguments();
    DiagnosticsOptions diagnosticsOptions = GetDiagnosticsOptions(commandLine);
    diagnosticsOptions.reportError = &ReportHeadlessDiagnosticsError;
    diagnosticsOptions.exit = true;

    InstallCrashReportHandler(diagnosticsOptions);

    const DiagnosticsOutputHandlers handlers = CreateHeadlessDiagnosticsOutputHandlers();
    const DiagnosticsValidationResult validation = ValidateDiagnosticsOptions(diagnosticsOptions, handlers);
    if (!validation.ok) {
        ReportDiagnosticsError(diagnosticsOptions, validation.message);
        return 2;
    }

    return RunDiagnosticsHeadlessMode(diagnosticsOptions, handlers);
}
