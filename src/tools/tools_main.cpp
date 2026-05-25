#include "tools/tools_main.h"

#include <cstdio>
#include <string>

#include "tools/format.h"
#include "tools/lint_check.h"

namespace {

void PrintUsage() {
    std::fprintf(stderr, "Usage:\n");
    std::fprintf(
        stderr,
        "  CaseDashTools.exe format [--style=file|--style=<path>|--style=file:<path>] [-i|-n|--dry-run] "
            "[-v|--verbose] [file...]\n"
    );
    std::fprintf(
        stderr,
        "  CaseDashTools.exe lint_check [--config <path>] [--check] [--no-progress] [--report-json <path>] "
            "[-v|--verbose]\n"
    );
}

}  // namespace

int RunToolsMain(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }
    const std::string tool = argv[1];
    if (tool == "format") {
        return RunFormat(argc - 2, argv + 2);
    }
    if (tool == "lint_check") {
        return RunLintCheck(argc - 2, argv + 2);
    }
    std::fprintf(stderr, "Unknown CaseDash tool: %s\n", tool.c_str());
    PrintUsage();
    return 2;
}

int main(int argc, char** argv) {
    return RunToolsMain(argc, argv);
}
