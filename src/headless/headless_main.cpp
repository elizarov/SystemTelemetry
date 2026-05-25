#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

#include "diagnostics/crash_report.h"
#include "diagnostics/diagnostics.h"
#include "headless/headless_command_line.h"
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
    if (IsHeadlessCommandLineHelpRequested(commandLine)) {
        PrintHeadlessCommandLineHelp(stdout);
        return 0;
    }

    HeadlessCommandLineConsumption commandLineConsumption;
    DiagnosticsOptions diagnosticsOptions =
        GetDiagnosticsOptions(commandLine, CreateHeadlessCommandLineTracker(commandLineConsumption, commandLine));
    const HeadlessCommandLineValidationResult commandLineValidation =
        ValidateHeadlessCommandLine(commandLine, commandLineConsumption);
    if (!commandLineValidation.ok) {
        PrintHeadlessCommandLineHelp(stderr, commandLineValidation.message);
        return 2;
    }

    diagnosticsOptions.reportError = &ReportHeadlessDiagnosticsError;
    diagnosticsOptions.exit = true;

    InstallCrashReportHandler(diagnosticsOptions);

    HeadlessLayoutGuideSheetOutputContext outputContext;
    std::string outputError;
    if (!InitializeHeadlessLayoutGuideSheetOutput(outputContext, &outputError)) {
        ReportDiagnosticsError(diagnosticsOptions, outputError);
        return 1;
    }
    const DiagnosticsOutputHandlers handlers = CreateHeadlessDiagnosticsOutputHandlers(outputContext);
    const DiagnosticsValidationResult validation = ValidateDiagnosticsOptions(diagnosticsOptions, handlers);
    if (!validation.ok) {
        PrintHeadlessCommandLineHelp(stderr, validation.message);
        return 2;
    }

    return RunDiagnosticsHeadlessMode(diagnosticsOptions, handlers);
}
