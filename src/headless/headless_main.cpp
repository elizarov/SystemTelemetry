#include <windows.h>

#include <cstdio>

#include "diagnostics/crash_report.h"
#include "diagnostics/diagnostics.h"
#include "headless/layout_guide_sheet_output.h"
#include "util/command_line.h"

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const CommandLineArguments commandLine = GetCommandLineArguments();
    DiagnosticsOptions diagnosticsOptions = GetDiagnosticsOptions(commandLine);
    diagnosticsOptions.exit = true;

    InstallCrashReportHandler(diagnosticsOptions);

    const DiagnosticsOutputHandlers handlers = CreateHeadlessDiagnosticsOutputHandlers();
    const DiagnosticsValidationResult validation = ValidateDiagnosticsOptions(diagnosticsOptions, handlers);
    if (!validation.ok) {
        std::fprintf(stderr, "%s\n", validation.message.c_str());
        return 2;
    }

    return RunDiagnosticsHeadlessMode(diagnosticsOptions, handlers);
}
