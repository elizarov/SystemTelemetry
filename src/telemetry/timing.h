#pragma once

#include <chrono>
#include <cstddef>

// Shared telemetry update cadence and live animation duration.
inline constexpr auto kTelemetryRefreshInterval = std::chrono::milliseconds(250);
inline constexpr double kTelemetryRefreshIntervalSeconds =
    static_cast<double>(kTelemetryRefreshInterval.count()) / 1000.0;

// Scalar ghost histories keep 120 raw 250 ms samples, which covers 30 seconds.
inline constexpr std::size_t kRetainedScalarHistorySamples = 120;

// 4 raw 250 ms samples make each throughput chart body point and live leader a 1 second average.
inline constexpr std::size_t kThroughputHistorySmoothingSamples = 4;
inline constexpr double kThroughputHistoryPointSeconds =
    kTelemetryRefreshIntervalSeconds * static_cast<double>(kThroughputHistorySmoothingSamples);

// Throughput chart bodies keep 30 ready-to-draw 1 Hz averages, which also covers 30 seconds.
inline constexpr std::size_t kRetainedThroughputHistorySamples = 30;

// 10 one-second throughput body points space throughput chart time markers every 10 seconds.
inline constexpr double kThroughputTimeMarkerIntervalSeconds = 10.0;
inline constexpr double kThroughputTimeMarkerIntervalSamples =
    kThroughputTimeMarkerIntervalSeconds / kThroughputHistoryPointSeconds;
