#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "telemetry/fps_provider.h"

inline constexpr wchar_t kFpsServiceName[] = L"CaseDashFpsService";
inline constexpr wchar_t kFpsServicePipeName[] = L"\\\\.\\pipe\\CaseDashFps";

std::vector<char> BuildFpsServiceRequest();
bool IsFpsServiceRequest(const void* data, size_t size);
std::vector<char> SerializeFpsServiceSample(const FpsTelemetrySample& sample);
std::optional<FpsTelemetrySample> ParseFpsServiceResponse(const void* data, size_t size, std::string& diagnostics);
