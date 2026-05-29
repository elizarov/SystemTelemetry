#pragma once

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

std::string FormatToolElapsed(std::chrono::steady_clock::duration elapsed);
bool IsToolOutputTerminal(FILE* output);

class ToolFileProgress {
public:
    ToolFileProgress(
        FILE* output,
        std::string_view label,
        size_t totalFiles,
        std::chrono::steady_clock::time_point started,
        bool enabled
    );
    ~ToolFileProgress();

    ToolFileProgress(const ToolFileProgress&) = delete;
    ToolFileProgress& operator=(const ToolFileProgress&) = delete;

    void Update(size_t completedFiles);
    void Finish(size_t completedFiles);

private:
    void Clear();

    FILE* output_ = nullptr;
    std::string label_;
    size_t totalFiles_ = 0;
    std::chrono::steady_clock::time_point started_;
    bool enabled_ = false;
    size_t previousLength_ = 0;
};
