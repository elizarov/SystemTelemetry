#include "telemetry/fps_service_protocol.h"

#include <cstring>

namespace {

constexpr uint32_t kCashDashServiceRequestMagic = 0x51524443;   // "CDRQ" little-endian.
constexpr uint32_t kCashDashServiceResponseMagic = 0x53524443;  // "CDRS" little-endian.
constexpr uint32_t kFpsServicePayloadMagic = 0x31535046;        // "FPS1" little-endian.
constexpr uint32_t kFpsServiceProtocolVersion = 1;
constexpr uint32_t kFpsServiceFlagAvailable = 1u << 0u;
constexpr uint32_t kFpsServiceFlagPermissionRequired = 1u << 1u;
constexpr uint32_t kFpsServiceFlagHasFps = 1u << 2u;
constexpr uint32_t kMaximumProtocolStringBytes = 4096;
constexpr uint32_t kMaximumProtocolPayloadBytes = 64 * 1024;
constexpr char kPresentedFpsSampleRequestName[] = "presented_fps_sample";

struct CashDashServiceRequestHeader {
    uint32_t magic = kCashDashServiceRequestMagic;
    uint32_t version = kFpsServiceProtocolVersion;
    uint32_t requestId = 0;
    uint32_t nameBytes = 0;
};

struct CashDashServiceResponseHeader {
    uint32_t magic = kCashDashServiceResponseMagic;
    uint32_t version = kFpsServiceProtocolVersion;
    uint32_t requestId = 0;
    uint32_t payloadBytes = 0;
};

struct FpsServiceResponseHeader {
    uint32_t magic = kFpsServicePayloadMagic;
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

std::string RequestName(CashDashServiceRequestId id) {
    switch (id) {
        case CashDashServiceRequestId::PresentedFpsSample:
            return kPresentedFpsSampleRequestName;
    }
    return {};
}

bool IsKnownRequestId(uint32_t id) {
    return id == static_cast<uint32_t>(CashDashServiceRequestId::PresentedFpsSample);
}

}  // namespace

std::vector<char> BuildCashDashServiceRequest(CashDashServiceRequestId id) {
    const std::string name = RequestName(id);
    const uint32_t nameBytes = StringSizeOrMax(name);

    CashDashServiceRequestHeader header;
    header.requestId = static_cast<uint32_t>(id);
    header.nameBytes = nameBytes;

    std::vector<char> output;
    output.reserve(sizeof(header) + nameBytes);
    AppendBytes(output, &header, sizeof(header));
    AppendBytes(output, name.data(), nameBytes);
    return output;
}

std::optional<CashDashServiceRequest> ParseCashDashServiceRequest(
    const void* data, size_t size, std::string& diagnostics) {
    diagnostics.clear();
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = size;

    CashDashServiceRequestHeader header;
    if (!ReadBytes(cursor, remaining, &header, sizeof(header))) {
        diagnostics = "CashDash service request is too short.";
        return std::nullopt;
    }
    if (header.magic != kCashDashServiceRequestMagic) {
        diagnostics = "CashDash service request magic does not match.";
        return std::nullopt;
    }
    if (header.version != kFpsServiceProtocolVersion) {
        diagnostics = "CashDash service request version is not supported.";
        return std::nullopt;
    }
    if (!IsKnownRequestId(header.requestId)) {
        diagnostics = "CashDash service request id is not supported.";
        return std::nullopt;
    }

    CashDashServiceRequest request;
    request.id = static_cast<CashDashServiceRequestId>(header.requestId);
    if (!ReadString(cursor, remaining, header.nameBytes, request.name) || remaining != 0) {
        diagnostics = "CashDash service request payload is malformed.";
        return std::nullopt;
    }
    if (request.name != RequestName(request.id)) {
        diagnostics = "CashDash service request name does not match its id.";
        return std::nullopt;
    }
    return request;
}

std::vector<char> BuildFpsServiceRequest() {
    return BuildCashDashServiceRequest(CashDashServiceRequestId::PresentedFpsSample);
}

bool IsFpsServiceRequest(const void* data, size_t size) {
    std::string diagnostics;
    const std::optional<CashDashServiceRequest> request = ParseCashDashServiceRequest(data, size, diagnostics);
    return request.has_value() && request->id == CashDashServiceRequestId::PresentedFpsSample;
}

std::vector<char> SerializeFpsServiceSample(const FpsTelemetrySample& sample) {
    const uint32_t processNameBytes = StringSizeOrMax(sample.processName);
    const uint32_t diagnosticsBytes = StringSizeOrMax(sample.diagnostics);

    FpsServiceResponseHeader payloadHeader;
    payloadHeader.flags = (sample.available ? kFpsServiceFlagAvailable : 0u) |
                          (sample.permissionRequired ? kFpsServiceFlagPermissionRequired : 0u) |
                          (sample.fps.has_value() ? kFpsServiceFlagHasFps : 0u);
    payloadHeader.fps = sample.fps.value_or(0.0);
    payloadHeader.processId = sample.processId;
    payloadHeader.processNameBytes = processNameBytes;
    payloadHeader.diagnosticsBytes = diagnosticsBytes;

    const uint32_t payloadBytes = sizeof(payloadHeader) + processNameBytes + diagnosticsBytes;
    CashDashServiceResponseHeader header;
    header.requestId = static_cast<uint32_t>(CashDashServiceRequestId::PresentedFpsSample);
    header.payloadBytes = payloadBytes;

    std::vector<char> output;
    output.reserve(sizeof(header) + payloadBytes);
    AppendBytes(output, &header, sizeof(header));
    AppendBytes(output, &payloadHeader, sizeof(payloadHeader));
    AppendBytes(output, sample.processName.data(), processNameBytes);
    AppendBytes(output, sample.diagnostics.data(), diagnosticsBytes);
    return output;
}

std::optional<FpsTelemetrySample> ParseFpsServiceResponse(const void* data, size_t size, std::string& diagnostics) {
    diagnostics.clear();
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = size;

    CashDashServiceResponseHeader header;
    if (!ReadBytes(cursor, remaining, &header, sizeof(header))) {
        diagnostics = "FPS service response is too short.";
        return std::nullopt;
    }
    if (header.magic != kCashDashServiceResponseMagic) {
        diagnostics = "FPS service response magic does not match.";
        return std::nullopt;
    }
    if (header.version != kFpsServiceProtocolVersion) {
        diagnostics = "FPS service response version is not supported.";
        return std::nullopt;
    }
    if (header.requestId != static_cast<uint32_t>(CashDashServiceRequestId::PresentedFpsSample)) {
        diagnostics = "FPS service response request id does not match.";
        return std::nullopt;
    }
    if (header.payloadBytes > kMaximumProtocolPayloadBytes || remaining != header.payloadBytes) {
        diagnostics = "FPS service response payload is malformed.";
        return std::nullopt;
    }

    FpsServiceResponseHeader payloadHeader;
    if (!ReadBytes(cursor, remaining, &payloadHeader, sizeof(payloadHeader))) {
        diagnostics = "FPS service response payload is too short.";
        return std::nullopt;
    }
    if (payloadHeader.magic != kFpsServicePayloadMagic) {
        diagnostics = "FPS service response payload magic does not match.";
        return std::nullopt;
    }
    if (payloadHeader.version != kFpsServiceProtocolVersion) {
        diagnostics = "FPS service response payload version is not supported.";
        return std::nullopt;
    }

    FpsTelemetrySample sample;
    sample.available = (payloadHeader.flags & kFpsServiceFlagAvailable) != 0;
    sample.permissionRequired = (payloadHeader.flags & kFpsServiceFlagPermissionRequired) != 0;
    if ((payloadHeader.flags & kFpsServiceFlagHasFps) != 0) {
        sample.fps = payloadHeader.fps;
    }
    sample.processId = payloadHeader.processId;

    if (!ReadString(cursor, remaining, payloadHeader.processNameBytes, sample.processName) ||
        !ReadString(cursor, remaining, payloadHeader.diagnosticsBytes, sample.diagnostics) || remaining != 0) {
        diagnostics = "FPS service response string payload is malformed.";
        return std::nullopt;
    }
    return sample;
}
