#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include "util/command_line.h"

struct HeadlessCommandLineValidationResult {
    bool ok = true;
    bool requestedHelp = false;
    std::string message;
};

HeadlessCommandLineValidationResult ValidateHeadlessCommandLine(const CommandLineArguments& commandLine);
void PrintHeadlessCommandLineHelp(std::FILE* stream, std::string_view message = {});
