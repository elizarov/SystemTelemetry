#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

class Trace;

struct GigabyteSivFanReading {
    std::string title;
    std::optional<double> rpm;
};

struct GigabyteSivTemperatureReading {
    std::string title;
    std::optional<double> celsius;
};

struct GigabyteSivSnapshot {
    bool success = false;
    std::string diagnostics;
    std::vector<GigabyteSivFanReading> fans;
    std::vector<GigabyteSivTemperatureReading> temperatures;
};

class GigabyteSivRuntime {
public:
    GigabyteSivRuntime();
    ~GigabyteSivRuntime();

    GigabyteSivRuntime(const GigabyteSivRuntime&) = delete;
    GigabyteSivRuntime& operator=(const GigabyteSivRuntime&) = delete;

    bool Capture(
        const std::wstring& sivDirectory, GigabyteSivSnapshot& snapshot, Trace& trace, std::string& diagnostics);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
