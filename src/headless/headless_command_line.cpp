#include "headless/headless_command_line.h"

#include <utility>

#include "util/text_format.h"

namespace {

std::string FormatArgument(std::string_view argument) {
    if (argument.empty()) {
        return "<empty>";
    }
    return FormatText("\"%.*s\"", static_cast<int>(argument.size()), argument.data());
}

void MarkConsumedArgument(void* context, size_t argumentIndex) {
    auto* consumption = static_cast<HeadlessCommandLineConsumption*>(context);
    if (consumption != nullptr && argumentIndex < consumption->consumedArguments.size()) {
        consumption->consumedArguments[argumentIndex] = 1;
    }
}

HeadlessCommandLineValidationResult HeadlessCommandLineFailure(std::string message) {
    HeadlessCommandLineValidationResult result;
    result.ok = false;
    result.message = std::move(message);
    return result;
}

}  // namespace

DiagnosticsCommandLineTracker CreateHeadlessCommandLineTracker(
    HeadlessCommandLineConsumption& consumption, const CommandLineArguments& commandLine) {
    consumption.consumedArguments.assign(commandLine.size(), 0);
    if (!consumption.consumedArguments.empty()) {
        consumption.consumedArguments[0] = 1;
    }
    return DiagnosticsCommandLineTracker{&consumption, &MarkConsumedArgument};
}

bool IsHeadlessCommandLineHelpRequested(const CommandLineArguments& commandLine) {
    return HasSwitch(commandLine, "/?") || HasSwitch(commandLine, "/help") || HasSwitch(commandLine, "-h") ||
           HasSwitch(commandLine, "--help");
}

HeadlessCommandLineValidationResult ValidateHeadlessCommandLine(
    const CommandLineArguments& commandLine, const HeadlessCommandLineConsumption& consumption) {
    for (size_t i = 1; i < commandLine.size(); ++i) {
        if (i >= consumption.consumedArguments.size() || consumption.consumedArguments[i] == 0) {
            return HeadlessCommandLineFailure(
                FormatText("Unknown or malformed command-line parameter: %s.", FormatArgument(commandLine[i]).c_str()));
        }
    }
    return {};
}

void PrintHeadlessCommandLineHelp(std::FILE* stream, std::string_view message) {
    if (stream == nullptr) {
        return;
    }
    if (!message.empty()) {
        std::fprintf(stream, "Error: %.*s\n\n", static_cast<int>(message.size()), message.data());
    }
    std::fputs("CaseDashHeadless.exe runs CaseDash diagnostics once and exits.\n"
               "\n"
               "Usage:\n"
               "  CaseDashHeadless.exe [switches]\n"
               "\n"
               "Output switches:\n"
               "  /trace[:path]                 Write trace output.\n"
               "  /trace-prefixes:<names>       Enable trace and filter comma-separated trace prefixes.\n"
               "  /dump[:path]                  Write a snapshot dump.\n"
               "  /screenshot[:path]            Write a dashboard PNG.\n"
               "  /layout-guide-sheet[:path]    Write a diagnostics layout guide sheet PNG.\n"
               "  /app-icon[:path]              Write the rendered app icon PNG.\n"
               "  /save-config[:path]           Write the minimal config overlay.\n"
               "  /save-full-config[:path]      Write the full config export.\n"
               "\n"
               "Source and config switches:\n"
               "  /fake[:path]                  Use built-in synthetic telemetry or a snapshot dump file.\n"
               "  /layout:<name>                Select a named layout.\n"
               "  /theme:<name>                 Select a named theme.\n"
               "  /default-config               Ignore executable-side config.ini for this run.\n"
               "  /scale:<value>                Override render scale with a positive number.\n"
               "  /app-icon-size:<pixels>       Set /app-icon size from 16 through 1024.\n"
               "\n"
               "Render switches:\n"
               "  /blank                        Export blank rendering mode.\n"
               "  /edit-layout[:target]         Enable layout-edit guides or a target preview.\n"
               "  /hover:<x>,<y>                Apply a layout-edit hover point during screenshot export.\n"
               "\n"
               "Control switches:\n"
               "  /exit                         Accepted for parity; headless always exits after one run.\n"
               "  /help or /?                   Print this help.\n"
               "\n"
               "Examples:\n"
               "  CaseDashHeadless.exe /default-config /fake /screenshot:build\\diagnostics\\screen.png\n"
               "  CaseDashHeadless.exe /default-config /fake /layout-guide-sheet:build\\diagnostics\\guide.png\n",
        stream);
}
