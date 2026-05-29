#include "tools/impl/tools_progress.h"

#include <cmath>
#include <io.h>

std::string FormatToolElapsed(std::chrono::steady_clock::duration elapsed) {
    const double seconds = std::chrono::duration<double>(elapsed).count();
    char buffer[64] = {};
    if (seconds < 1.0) {
        std::snprintf(buffer, sizeof(buffer), "%dms", static_cast<int>(std::llround(seconds * 1000.0)));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.3fs", seconds);
    }
    return buffer;
}

bool IsToolOutputTerminal(FILE* output) {
    return output != nullptr && _isatty(_fileno(output)) != 0;
}

ToolFileProgress::ToolFileProgress(
    FILE* output,
    std::string_view label,
    size_t totalFiles,
    std::chrono::steady_clock::time_point started,
    bool enabled
) :
    output_(output),
    label_(label),
    totalFiles_(totalFiles),
    started_(started),
    enabled_(enabled && IsToolOutputTerminal(output)) {}

ToolFileProgress::~ToolFileProgress() {
    Clear();
}

void ToolFileProgress::Update(size_t completedFiles) {
    if (!enabled_) {
        return;
    }
    const std::string progress = label_ +
        " completed " +
        std::to_string(completedFiles) +
        "/" +
        std::to_string(totalFiles_) +
        " files in " +
        FormatToolElapsed(std::chrono::steady_clock::now() - started_);
    const std::string padding(previousLength_ > progress.size() ? previousLength_ - progress.size() : 0, ' ');
    std::fprintf(output_, "\r%s%s", progress.c_str(), padding.c_str());
    std::fflush(output_);
    previousLength_ = progress.size();
}

void ToolFileProgress::Finish(size_t completedFiles) {
    Update(completedFiles);
    Clear();
}

void ToolFileProgress::Clear() {
    if (!enabled_ || previousLength_ == 0) {
        return;
    }
    std::fprintf(output_, "\r%s\r", std::string(previousLength_, ' ').c_str());
    std::fflush(output_);
    previousLength_ = 0;
}
