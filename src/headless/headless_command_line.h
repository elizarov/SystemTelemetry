#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics/diagnostics.h"
#include "util/command_line.h"

struct HeadlessCommandLineValidationResult {
    bool ok = true;
    std::string message;
};

struct HeadlessCommandLineConsumption {
    std::vector<unsigned char> consumedArguments;
};

DiagnosticsCommandLineTracker CreateHeadlessCommandLineTracker(
    HeadlessCommandLineConsumption& consumption, const CommandLineArguments& commandLine);
bool IsHeadlessCommandLineHelpRequested(const CommandLineArguments& commandLine);
HeadlessCommandLineValidationResult ValidateHeadlessCommandLine(
    const CommandLineArguments& commandLine, const HeadlessCommandLineConsumption& consumption);
void PrintHeadlessCommandLineHelp(std::FILE* stream, std::string_view message = {});
