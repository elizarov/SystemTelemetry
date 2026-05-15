#include "util/trace_timing.h"

#include <algorithm>
#include <utility>

#include "util/numeric_format.h"
#include "util/trace.h"

namespace {

double Milliseconds(std::chrono::nanoseconds elapsed) {
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

}  // namespace

TraceTimingScope::TraceTimingScope(TraceTimingCollector& collector, const Trace& trace, const char* operation)
    : collector_(&collector), trace_(&trace), operation_(operation), startedAt_(HighPrecisionTimer::Now()) {}

TraceTimingScope::TraceTimingScope(TraceTimingScope&& other) noexcept
    : collector_(std::exchange(other.collector_, nullptr)), trace_(std::exchange(other.trace_, nullptr)),
      operation_(std::exchange(other.operation_, nullptr)), startedAt_(std::exchange(other.startedAt_, 0)) {}

TraceTimingScope& TraceTimingScope::operator=(TraceTimingScope&& other) noexcept {
    if (this != &other) {
        Reset();
        collector_ = std::exchange(other.collector_, nullptr);
        trace_ = std::exchange(other.trace_, nullptr);
        operation_ = std::exchange(other.operation_, nullptr);
        startedAt_ = std::exchange(other.startedAt_, 0);
    }
    return *this;
}

TraceTimingScope::~TraceTimingScope() {
    Reset();
}

void TraceTimingScope::Reset() {
    if (collector_ == nullptr || trace_ == nullptr || operation_ == nullptr) {
        return;
    }
    const HighPrecisionTimer::Tick endedAt = HighPrecisionTimer::Now();
    collector_->Record(*trace_, operation_, HighPrecisionTimer::Elapsed(startedAt_, endedAt));
    collector_ = nullptr;
    trace_ = nullptr;
    operation_ = nullptr;
    startedAt_ = 0;
}

TraceTimingCollector::TraceTimingCollector(std::chrono::seconds flushInterval) : flushInterval_(flushInterval) {}

TraceTimingScope TraceTimingCollector::Measure(const Trace& trace, const char* operation) {
    if (!trace.Enabled(TracePrefix::Profile) || operation == nullptr) {
        return {};
    }
    return TraceTimingScope(*this, trace, operation);
}

void TraceTimingCollector::Record(const Trace& trace, std::string_view operation, std::chrono::nanoseconds elapsed) {
    if (!trace.Enabled(TracePrefix::Profile) || operation.empty() || elapsed.count() <= 0) {
        return;
    }

    std::vector<OperationStats> snapshot;
    std::chrono::nanoseconds intervalElapsed{};
    {
        const LightweightMutexLock lock(mutex_);
        const HighPrecisionTimer::Tick now = HighPrecisionTimer::Now();
        if (lastFlushAt_ == 0) {
            lastFlushAt_ = now;
        }

        auto found = std::find_if(
            stats_.begin(), stats_.end(), [&](const OperationStats& stats) { return stats.operation == operation; });
        if (found == stats_.end()) {
            OperationStats stats;
            stats.operation = std::string(operation);
            stats.total = elapsed;
            stats.samples = 1;
            stats_.push_back(std::move(stats));
        } else {
            found->total += elapsed;
            ++found->samples;
        }

        intervalElapsed = HighPrecisionTimer::Elapsed(lastFlushAt_, now);
        if (intervalElapsed < flushInterval_) {
            return;
        }
        snapshot = std::move(stats_);
        stats_.clear();
        lastFlushAt_ = now;
    }

    EmitSnapshot(trace, std::move(snapshot), intervalElapsed);
}

void TraceTimingCollector::Flush(const Trace& trace) {
    if (!trace.Enabled(TracePrefix::Profile)) {
        return;
    }

    std::vector<OperationStats> snapshot;
    std::chrono::nanoseconds intervalElapsed{};
    {
        const LightweightMutexLock lock(mutex_);
        if (stats_.empty()) {
            lastFlushAt_ = HighPrecisionTimer::Now();
            return;
        }
        const HighPrecisionTimer::Tick now = HighPrecisionTimer::Now();
        intervalElapsed =
            lastFlushAt_ == 0 ? std::chrono::nanoseconds{} : HighPrecisionTimer::Elapsed(lastFlushAt_, now);
        snapshot = std::move(stats_);
        stats_.clear();
        lastFlushAt_ = now;
    }

    EmitSnapshot(trace, std::move(snapshot), intervalElapsed);
}

void TraceTimingCollector::Reset() {
    const LightweightMutexLock lock(mutex_);
    stats_.clear();
    lastFlushAt_ = HighPrecisionTimer::Now();
}

void TraceTimingCollector::EmitSnapshot(
    const Trace& trace, std::vector<OperationStats> stats, std::chrono::nanoseconds intervalElapsed) const {
    const std::string intervalText = FormatDoubleFixed(Milliseconds(intervalElapsed), 3);
    for (const OperationStats& entry : stats) {
        if (entry.samples == 0) {
            continue;
        }
        const double totalMs = Milliseconds(entry.total);
        const double averageMs = totalMs / static_cast<double>(entry.samples);
        trace.Write(TracePrefix::Profile,
            "timing op=" + Trace::QuoteText(entry.operation) + " samples=" + std::to_string(entry.samples) +
                " total_ms=" + FormatDoubleFixed(totalMs, 3) + " avg_ms=" + FormatDoubleFixed(averageMs, 3) +
                " interval_ms=" + intervalText);
    }
}
