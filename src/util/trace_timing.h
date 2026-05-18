#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "util/high_precision_timer.h"
#include "util/lightweight_mutex.h"

class Trace;
class TraceTimingCollector;

class TraceTimingScope final {
public:
    TraceTimingScope() = default;
    TraceTimingScope(TraceTimingCollector& collector, const Trace& trace, const char* operation);
    TraceTimingScope(const TraceTimingScope&) = delete;
    TraceTimingScope& operator=(const TraceTimingScope&) = delete;
    TraceTimingScope(TraceTimingScope&& other) noexcept;
    TraceTimingScope& operator=(TraceTimingScope&& other) noexcept;
    ~TraceTimingScope();

private:
    void Reset();

    TraceTimingCollector* collector_ = nullptr;
    const Trace* trace_ = nullptr;
    const char* operation_ = nullptr;
    HighPrecisionTimer::Tick startedAt_ = 0;
};

class TraceTimingCollector final {
public:
    explicit TraceTimingCollector(std::chrono::seconds flushInterval = std::chrono::seconds(10));

    TraceTimingScope Measure(const Trace& trace, const char* operation);
    void Record(const Trace& trace, std::string_view operation, std::chrono::nanoseconds elapsed);
    void Flush(const Trace& trace);
    void Reset();

private:
    struct OperationStats {
        std::string operation;
        std::chrono::nanoseconds total{};
        std::uint64_t samples = 0;
    };

    void EmitSnapshot(
        const Trace& trace, std::vector<OperationStats> stats, std::chrono::nanoseconds intervalElapsed) const;

    mutable LightweightMutex mutex_;
    std::vector<OperationStats> stats_;
    std::chrono::nanoseconds flushInterval_;
    HighPrecisionTimer::Tick lastFlushAt_ = 0;
};
