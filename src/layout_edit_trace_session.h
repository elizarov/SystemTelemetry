#pragma once

#include <chrono>
#include <ostream>
#include <string>

#include "layout_edit_controller.h"

class LayoutEditTraceSession {
public:
    void Begin(std::ostream* traceStream, const std::string& kind, const std::string& detail);
    void Record(LayoutEditHost::TracePhase phase, std::chrono::nanoseconds elapsed);
    void End(std::ostream* traceStream, const std::string& reason);

private:
    struct Stats {
        std::chrono::nanoseconds total{};
        size_t samples = 0;
    };

    bool active_ = false;
    std::string kind_;
    std::string detail_;
    std::chrono::steady_clock::time_point startedAt_{};
    Stats snap_{};
    Stats apply_{};
    Stats paintTotal_{};
    Stats paintDraw_{};
};
