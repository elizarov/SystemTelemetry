#pragma once

#include <chrono>
#include <cstdint>

class HighPrecisionTimer final {
public:
    using Tick = std::int64_t;

    static Tick Now();
    static std::chrono::nanoseconds Elapsed(Tick start, Tick end);
    static std::chrono::nanoseconds Since(Tick start);
};
