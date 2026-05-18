#include "util/high_precision_timer.h"

#include <windows.h>

#include <limits>

namespace {

std::int64_t QueryFrequency() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart > 0 ? frequency.QuadPart : 1;
}

std::int64_t PerformanceFrequency() {
    static const std::int64_t frequency = QueryFrequency();
    return frequency;
}

}  // namespace

HighPrecisionTimer::Tick HighPrecisionTimer::Now() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

std::chrono::nanoseconds HighPrecisionTimer::Elapsed(Tick start, Tick end) {
    if (end <= start) {
        return {};
    }
    const long double ticks = static_cast<long double>(end - start);
    const long double frequency = static_cast<long double>(PerformanceFrequency());
    const long double nanoseconds = ticks * 1000000000.0L / frequency;
    constexpr long double kMaxNanoseconds =
        static_cast<long double>(std::numeric_limits<std::chrono::nanoseconds::rep>::max());
    if (nanoseconds >= kMaxNanoseconds) {
        return std::chrono::nanoseconds::max();
    }
    return std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(nanoseconds));
}

std::chrono::nanoseconds HighPrecisionTimer::Since(Tick start) {
    return Elapsed(start, Now());
}
