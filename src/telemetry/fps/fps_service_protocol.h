#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "telemetry/fps/fps_etw_provider.h"

inline constexpr wchar_t kFpsServiceName[] = L"SystemTelemetryFpsService";
inline constexpr wchar_t kFpsServicePipeName[] = L"\\\\.\\pipe\\SystemTelemetryFps";

std::vector<char> BuildFpsServiceRequest();
bool IsFpsServiceRequest(const void* data, size_t size);
std::vector<char> SerializeFpsServiceSample(const FpsTelemetrySample& sample);
std::optional<FpsTelemetrySample> ParseFpsServiceResponse(const void* data, size_t size, std::string& diagnostics);
