#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "telemetry/fps_provider.h"

inline constexpr char kFpsServiceName[] = "CashDashService";
inline constexpr char kFpsServicePipeName[] = "\\\\.\\pipe\\CashDashService";

enum class CashDashServiceRequestId : uint32_t {
    PresentedFpsSample = 1,
};

struct CashDashServiceRequest {
    CashDashServiceRequestId id = CashDashServiceRequestId::PresentedFpsSample;
    std::string name;
    FpsTelemetrySampleOptions fpsOptions;
};

std::vector<char> BuildCashDashServiceRequest(
    CashDashServiceRequestId id, const FpsTelemetrySampleOptions& fpsOptions = {});
std::optional<CashDashServiceRequest> ParseCashDashServiceRequest(
    const void* data, size_t size, std::string& diagnostics);
std::vector<char> BuildFpsServiceRequest(const FpsTelemetrySampleOptions& options = {});
bool IsFpsServiceRequest(const void* data, size_t size);
std::vector<char> SerializeFpsServiceSample(const FpsTelemetrySample& sample);
std::optional<FpsTelemetrySample> ParseFpsServiceResponse(const void* data, size_t size, std::string& diagnostics);
