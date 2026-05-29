#pragma once

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

enum class FormatMode {
    Stdout,
    InPlace,
    DryRun,
};

struct FormatOptions {
    FormatMode mode = FormatMode::Stdout;
    bool verbose = false;
    bool help = false;
    bool fileListProvided = false;
    size_t concurrency = 0;
    std::optional<std::string> explicitStylePath;
    std::vector<std::string> files;
};

std::optional<FormatOptions> ParseFormatArgs(int argc, char** argv, std::string& error);
void PrintFormatUsage(FILE* output);
