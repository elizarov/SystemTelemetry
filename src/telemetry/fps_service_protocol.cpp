#include "telemetry/fps_service_protocol.h"

#include <cstring>

namespace {

constexpr char kFpsServiceRequestText[] = "casedash_fps_sample_v1";
constexpr uint32_t kFpsServiceResponseMagic = 0x31535046;  // "FPS1" little-endian.
constexpr uint32_t kFpsServiceProtocolVersion = 1;
constexpr uint32_t kFpsServiceFlagAvailable = 1u << 0u;
constexpr uint32_t kFpsServiceFlagPermissionRequired = 1u << 1u;
constexpr uint32_t kFpsServiceFlagHasFps = 1u << 2u;
constexpr uint32_t kMaximumProtocolStringBytes = 4096;

struct FpsServiceResponseHeader {
    uint32_t magic = kFpsServiceResponseMagic;
    uint32_t version = kFpsServiceProtocolVersion;
    uint32_t flags = 0;
    double fps = 0.0;
    uint32_t processId = 0;
    uint32_t processNameBytes = 0;
    uint32_t diagnosticsBytes = 0;
};

void AppendBytes(std::vector<char>& output, const void* data, size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    output.insert(output.end(), bytes, bytes + size);
}

bool ReadBytes(const char*& cursor, size_t& remaining, void* output, size_t size) {
    if (remaining < size) {
        return false;
    }
    memcpy(output, cursor, size);
    cursor += size;
    remaining -= size;
    return true;
}

bool ReadString(const char*& cursor, size_t& remaining, uint32_t size, std::string& output) {
    if (size > kMaximumProtocolStringBytes || remaining < size) {
        return false;
    }
    output.assign(cursor, cursor + size);
    cursor += size;
    remaining -= size;
    return true;
}

uint32_t StringSizeOrMax(const std::string& text) {
    return text.size() > kMaximumProtocolStringBytes ? kMaximumProtocolStringBytes : static_cast<uint32_t>(text.size());
}

}  // namespace

std::vector<char> BuildFpsServiceRequest() {
    return std::vector<char>(kFpsServiceRequestText, kFpsServiceRequestText + sizeof(kFpsServiceRequestText) - 1);
}

bool IsFpsServiceRequest(const void* data, size_t size) {
    const std::vector<char> request = BuildFpsServiceRequest();
    return size == request.size() && memcmp(data, request.data(), request.size()) == 0;
}

std::vector<char> SerializeFpsServiceSample(const FpsTelemetrySample& sample) {
    const uint32_t processNameBytes = StringSizeOrMax(sample.processName);
    const uint32_t diagnosticsBytes = StringSizeOrMax(sample.diagnostics);

    FpsServiceResponseHeader header;
    header.flags = (sample.available ? kFpsServiceFlagAvailable : 0u) |
                   (sample.permissionRequired ? kFpsServiceFlagPermissionRequired : 0u) |
                   (sample.fps.has_value() ? kFpsServiceFlagHasFps : 0u);
    header.fps = sample.fps.value_or(0.0);
    header.processId = sample.processId;
    header.processNameBytes = processNameBytes;
    header.diagnosticsBytes = diagnosticsBytes;

    std::vector<char> output;
    output.reserve(sizeof(header) + processNameBytes + diagnosticsBytes);
    AppendBytes(output, &header, sizeof(header));
    AppendBytes(output, sample.processName.data(), processNameBytes);
    AppendBytes(output, sample.diagnostics.data(), diagnosticsBytes);
    return output;
}

std::optional<FpsTelemetrySample> ParseFpsServiceResponse(const void* data, size_t size, std::string& diagnostics) {
    diagnostics.clear();
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = size;

    FpsServiceResponseHeader header;
    if (!ReadBytes(cursor, remaining, &header, sizeof(header))) {
        diagnostics = "FPS service response is too short.";
        return std::nullopt;
    }
    if (header.magic != kFpsServiceResponseMagic) {
        diagnostics = "FPS service response magic does not match.";
        return std::nullopt;
    }
    if (header.version != kFpsServiceProtocolVersion) {
        diagnostics = "FPS service response version is not supported.";
        return std::nullopt;
    }

    FpsTelemetrySample sample;
    sample.available = (header.flags & kFpsServiceFlagAvailable) != 0;
    sample.permissionRequired = (header.flags & kFpsServiceFlagPermissionRequired) != 0;
    if ((header.flags & kFpsServiceFlagHasFps) != 0) {
        sample.fps = header.fps;
    }
    sample.processId = header.processId;

    if (!ReadString(cursor, remaining, header.processNameBytes, sample.processName) ||
        !ReadString(cursor, remaining, header.diagnosticsBytes, sample.diagnostics) || remaining != 0) {
        diagnostics = "FPS service response string payload is malformed.";
        return std::nullopt;
    }
    return sample;
}
