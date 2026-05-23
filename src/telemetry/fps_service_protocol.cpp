#include "telemetry/fps_service_protocol.h"

#include <cstring>
#include <utility>

#include "util/resource_strings.h"

namespace {

constexpr uint32_t kCashDashServiceRequestMagic = 0x51524443;  // "CDRQ" little-endian.
constexpr uint32_t kCashDashServiceResponseMagic = 0x53524443;  // "CDRS" little-endian.
constexpr uint32_t kFpsServicePayloadMagic = 0x31535046;  // "FPS1" little-endian.
constexpr uint32_t kBoardSensorsServicePayloadMagic = 0x31534442;  // "BDS1" little-endian.
constexpr uint32_t kFpsServiceProtocolVersion = 2;
constexpr uint32_t kFpsServiceFlagAvailable = 1u << 0u;
constexpr uint32_t kFpsServiceFlagPermissionRequired = 1u << 1u;
constexpr uint32_t kFpsServiceFlagHasFps = 1u << 2u;
constexpr uint32_t kBoardSensorsServiceFlagAvailable = 1u << 0u;
constexpr uint32_t kBoardSensorsServiceFlagHasValue = 1u << 0u;
constexpr uint32_t kMaximumProtocolStringBytes = 4096;
constexpr uint32_t kMaximumProtocolPayloadBytes = 64 * 1024;
constexpr uint32_t kMaximumBoardSensorCount = 64;
constexpr char kPresentedFpsSampleRequestName[] = "presented_fps_sample";
constexpr char kBoardSensorsSampleRequestName[] = "board_sensors_sample";

struct CashDashServiceRequestHeader {
    uint32_t magic = kCashDashServiceRequestMagic;
    uint32_t version = kFpsServiceProtocolVersion;
    uint32_t requestId = 0;
    uint32_t nameBytes = 0;
    uint32_t fpsAdapterLuidTokenBytes = 0;
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

struct BoardSensorsServiceResponseHeader {
    uint32_t magic = kBoardSensorsServicePayloadMagic;
    uint32_t version = kFpsServiceProtocolVersion;
    uint32_t flags = 0;
    uint32_t boardManufacturerBytes = 0;
    uint32_t boardProductBytes = 0;
    uint32_t driverLibraryBytes = 0;
    uint32_t providerNameBytes = 0;
    uint32_t diagnosticsBytes = 0;
    uint32_t requestedFanCount = 0;
    uint32_t requestedTemperatureCount = 0;
    uint32_t availableFanCount = 0;
    uint32_t availableTemperatureCount = 0;
    uint32_t fanCount = 0;
    uint32_t temperatureCount = 0;
};

struct BoardMetricServiceRecordHeader {
    uint32_t nameBytes = 0;
    uint32_t unit = 0;
    uint32_t issue = 0;
    uint32_t flags = 0;
    double value = 0.0;
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

uint32_t CountSizeOrMax(size_t size) {
    return size > kMaximumBoardSensorCount ? kMaximumBoardSensorCount : static_cast<uint32_t>(size);
}

void AppendStringPayload(std::vector<char>& output, const std::string& text, uint32_t bytes) {
    AppendBytes(output, text.data(), bytes);
}

void AppendCountedString(std::vector<char>& output, const std::string& text) {
    const uint32_t bytes = StringSizeOrMax(text);
    AppendBytes(output, &bytes, sizeof(bytes));
    AppendStringPayload(output, text, bytes);
}

void AppendStringVector(std::vector<char>& output, const std::vector<std::string>& values, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        AppendCountedString(output, values[i]);
    }
}

bool ReadCountedString(const char*& cursor, size_t& remaining, std::string& output) {
    uint32_t bytes = 0;
    return ReadBytes(cursor, remaining, &bytes, sizeof(bytes)) && ReadString(cursor, remaining, bytes, output);
}

bool ReadStringVector(const char*& cursor, size_t& remaining, uint32_t count, std::vector<std::string>& output) {
    if (count > kMaximumBoardSensorCount) {
        return false;
    }
    output.clear();
    output.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::string value;
        if (!ReadCountedString(cursor, remaining, value)) {
            return false;
        }
        output.push_back(std::move(value));
    }
    return true;
}

void AppendMetricVector(std::vector<char>& output, const std::vector<NamedScalarMetric>& metrics, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const NamedScalarMetric& metric = metrics[i];
        BoardMetricServiceRecordHeader header;
        header.nameBytes = StringSizeOrMax(metric.name);
        header.unit = static_cast<uint32_t>(metric.metric.unit);
        header.issue = static_cast<uint32_t>(metric.metric.issue);
        if (metric.metric.value.has_value()) {
            header.flags |= kBoardSensorsServiceFlagHasValue;
            header.value = *metric.metric.value;
        }
        AppendBytes(output, &header, sizeof(header));
        AppendStringPayload(output, metric.name, header.nameBytes);
    }
}

bool ReadMetricVector(const char*& cursor, size_t& remaining, uint32_t count, std::vector<NamedScalarMetric>& output) {
    if (count > kMaximumBoardSensorCount) {
        return false;
    }
    output.clear();
    output.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        BoardMetricServiceRecordHeader header;
        if (!ReadBytes(cursor, remaining, &header, sizeof(header))) {
            return false;
        }

        NamedScalarMetric metric;
        if (!ReadString(cursor, remaining, header.nameBytes, metric.name)) {
            return false;
        }
        metric.metric.unit = static_cast<ScalarMetricUnit>(header.unit);
        metric.metric.issue = static_cast<ScalarMetricIssue>(header.issue);
        if ((header.flags & kBoardSensorsServiceFlagHasValue) != 0) {
            metric.metric.value = header.value;
        }
        output.push_back(std::move(metric));
    }
    return true;
}

std::string RequestName(CashDashServiceRequestId id) {
    switch (id) {
        case CashDashServiceRequestId::PresentedFpsSample:
            return kPresentedFpsSampleRequestName;
        case CashDashServiceRequestId::BoardSensorsSample:
            return kBoardSensorsSampleRequestName;
    }
    return {};
}

bool IsKnownRequestId(uint32_t id) {
    return id == static_cast<uint32_t>(CashDashServiceRequestId::PresentedFpsSample) ||
        id == static_cast<uint32_t>(CashDashServiceRequestId::BoardSensorsSample);
}

}  // namespace

std::vector<char> BuildCashDashServiceRequest(
    CashDashServiceRequestId id,
    const FpsTelemetrySampleOptions& fpsOptions
) {
    const std::string name = RequestName(id);
    const uint32_t nameBytes = StringSizeOrMax(name);
    const uint32_t adapterLuidTokenBytes =
        id == CashDashServiceRequestId::PresentedFpsSample ? StringSizeOrMax(fpsOptions.gpuAdapterLuidToken) : 0;

    CashDashServiceRequestHeader header;
    header.requestId = static_cast<uint32_t>(id);
    header.nameBytes = nameBytes;
    header.fpsAdapterLuidTokenBytes = adapterLuidTokenBytes;

    std::vector<char> output;
    output.reserve(sizeof(header) + nameBytes + adapterLuidTokenBytes);
    AppendBytes(output, &header, sizeof(header));
    AppendBytes(output, name.data(), nameBytes);
    AppendStringPayload(output, fpsOptions.gpuAdapterLuidToken, adapterLuidTokenBytes);
    return output;
}

std::optional<CashDashServiceRequest> ParseCashDashServiceRequest(
    const void* data,
    size_t size,
    std::string& diagnostics
) {
    diagnostics.clear();
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = size;

    CashDashServiceRequestHeader header;
    if (!ReadBytes(cursor, remaining, &header, sizeof(header))) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request is too short."));
        return std::nullopt;
    }
    if (header.magic != kCashDashServiceRequestMagic) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request magic does not match."));
        return std::nullopt;
    }
    if (header.version != kFpsServiceProtocolVersion) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request version is not supported."));
        return std::nullopt;
    }
    if (!IsKnownRequestId(header.requestId)) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request id is not supported."));
        return std::nullopt;
    }

    CashDashServiceRequest request;
    request.id = static_cast<CashDashServiceRequestId>(header.requestId);
    if (
        !ReadString(cursor, remaining, header.nameBytes, request.name) ||
        !ReadString(cursor, remaining, header.fpsAdapterLuidTokenBytes, request.fpsOptions.gpuAdapterLuidToken) ||
        remaining != 0
    ) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request payload is malformed."));
        return std::nullopt;
    }
    if (request.name != RequestName(request.id)) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request name does not match its id."));
        return std::nullopt;
    }
    if (request.id != CashDashServiceRequestId::PresentedFpsSample && !request.fpsOptions.gpuAdapterLuidToken.empty()) {
        diagnostics = ResourceStringText(RES_STR("CashDash service request payload is malformed."));
        return std::nullopt;
    }
    return request;
}

std::vector<char> BuildFpsServiceRequest(const FpsTelemetrySampleOptions& options) {
    return BuildCashDashServiceRequest(CashDashServiceRequestId::PresentedFpsSample, options);
}

std::vector<char> BuildBoardSensorsServiceRequest() {
    return BuildCashDashServiceRequest(CashDashServiceRequestId::BoardSensorsSample);
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
        diagnostics = ResourceStringText(RES_STR("FPS service response is too short."));
        return std::nullopt;
    }
    if (header.magic != kCashDashServiceResponseMagic) {
        diagnostics = ResourceStringText(RES_STR("FPS service response magic does not match."));
        return std::nullopt;
    }
    if (header.version != kFpsServiceProtocolVersion) {
        diagnostics = ResourceStringText(RES_STR("FPS service response version is not supported."));
        return std::nullopt;
    }
    if (header.requestId != static_cast<uint32_t>(CashDashServiceRequestId::PresentedFpsSample)) {
        diagnostics = ResourceStringText(RES_STR("FPS service response request id does not match."));
        return std::nullopt;
    }
    if (header.payloadBytes > kMaximumProtocolPayloadBytes || remaining != header.payloadBytes) {
        diagnostics = ResourceStringText(RES_STR("FPS service response payload is malformed."));
        return std::nullopt;
    }

    FpsServiceResponseHeader payloadHeader;
    if (!ReadBytes(cursor, remaining, &payloadHeader, sizeof(payloadHeader))) {
        diagnostics = ResourceStringText(RES_STR("FPS service response payload is too short."));
        return std::nullopt;
    }
    if (payloadHeader.magic != kFpsServicePayloadMagic) {
        diagnostics = ResourceStringText(RES_STR("FPS service response payload magic does not match."));
        return std::nullopt;
    }
    if (payloadHeader.version != kFpsServiceProtocolVersion) {
        diagnostics = ResourceStringText(RES_STR("FPS service response payload version is not supported."));
        return std::nullopt;
    }

    FpsTelemetrySample sample;
    sample.available = (payloadHeader.flags & kFpsServiceFlagAvailable) != 0;
    sample.permissionRequired = (payloadHeader.flags & kFpsServiceFlagPermissionRequired) != 0;
    if ((payloadHeader.flags & kFpsServiceFlagHasFps) != 0) {
        sample.fps = payloadHeader.fps;
    }
    sample.processId = payloadHeader.processId;

    if (
        !ReadString(cursor, remaining, payloadHeader.processNameBytes, sample.processName) ||
        !ReadString(cursor, remaining, payloadHeader.diagnosticsBytes, sample.diagnostics) ||
        remaining != 0
    ) {
        diagnostics = ResourceStringText(RES_STR("FPS service response string payload is malformed."));
        return std::nullopt;
    }
    return sample;
}

std::vector<char> SerializeBoardSensorsServiceSample(const BoardVendorTelemetrySample& sample) {
    const uint32_t boardManufacturerBytes = StringSizeOrMax(sample.boardManufacturer);
    const uint32_t boardProductBytes = StringSizeOrMax(sample.boardProduct);
    const uint32_t driverLibraryBytes = StringSizeOrMax(sample.driverLibrary);
    const uint32_t providerNameBytes = StringSizeOrMax(sample.providerName);
    const uint32_t diagnosticsBytes = StringSizeOrMax(sample.diagnostics);
    const uint32_t requestedFanCount = CountSizeOrMax(sample.requestedFanNames.size());
    const uint32_t requestedTemperatureCount = CountSizeOrMax(sample.requestedTemperatureNames.size());
    const uint32_t availableFanCount = CountSizeOrMax(sample.availableFanNames.size());
    const uint32_t availableTemperatureCount = CountSizeOrMax(sample.availableTemperatureNames.size());
    const uint32_t fanCount = CountSizeOrMax(sample.fans.size());
    const uint32_t temperatureCount = CountSizeOrMax(sample.temperatures.size());

    BoardSensorsServiceResponseHeader payloadHeader;
    payloadHeader.flags = sample.available ? kBoardSensorsServiceFlagAvailable : 0u;
    payloadHeader.boardManufacturerBytes = boardManufacturerBytes;
    payloadHeader.boardProductBytes = boardProductBytes;
    payloadHeader.driverLibraryBytes = driverLibraryBytes;
    payloadHeader.providerNameBytes = providerNameBytes;
    payloadHeader.diagnosticsBytes = diagnosticsBytes;
    payloadHeader.requestedFanCount = requestedFanCount;
    payloadHeader.requestedTemperatureCount = requestedTemperatureCount;
    payloadHeader.availableFanCount = availableFanCount;
    payloadHeader.availableTemperatureCount = availableTemperatureCount;
    payloadHeader.fanCount = fanCount;
    payloadHeader.temperatureCount = temperatureCount;

    std::vector<char> payload;
    payload.reserve(1024);
    AppendBytes(payload, &payloadHeader, sizeof(payloadHeader));
    AppendStringPayload(payload, sample.boardManufacturer, boardManufacturerBytes);
    AppendStringPayload(payload, sample.boardProduct, boardProductBytes);
    AppendStringPayload(payload, sample.driverLibrary, driverLibraryBytes);
    AppendStringPayload(payload, sample.providerName, providerNameBytes);
    AppendStringPayload(payload, sample.diagnostics, diagnosticsBytes);
    AppendStringVector(payload, sample.requestedFanNames, requestedFanCount);
    AppendStringVector(payload, sample.requestedTemperatureNames, requestedTemperatureCount);
    AppendStringVector(payload, sample.availableFanNames, availableFanCount);
    AppendStringVector(payload, sample.availableTemperatureNames, availableTemperatureCount);
    AppendMetricVector(payload, sample.fans, fanCount);
    AppendMetricVector(payload, sample.temperatures, temperatureCount);

    CashDashServiceResponseHeader header;
    header.requestId = static_cast<uint32_t>(CashDashServiceRequestId::BoardSensorsSample);
    header.payloadBytes = static_cast<uint32_t>(payload.size());

    std::vector<char> output;
    output.reserve(sizeof(header) + payload.size());
    AppendBytes(output, &header, sizeof(header));
    AppendBytes(output, payload.data(), payload.size());
    return output;
}

std::optional<BoardVendorTelemetrySample> ParseBoardSensorsServiceResponse(
    const void* data,
    size_t size,
    std::string& diagnostics
) {
    diagnostics.clear();
    const char* cursor = static_cast<const char*>(data);
    size_t remaining = size;

    CashDashServiceResponseHeader header;
    if (!ReadBytes(cursor, remaining, &header, sizeof(header))) {
        diagnostics = "Board sensor service response is too short.";
        return std::nullopt;
    }
    if (header.magic != kCashDashServiceResponseMagic) {
        diagnostics = "Board sensor service response magic does not match.";
        return std::nullopt;
    }
    if (header.version != kFpsServiceProtocolVersion) {
        diagnostics = "Board sensor service response version is not supported.";
        return std::nullopt;
    }
    if (header.requestId != static_cast<uint32_t>(CashDashServiceRequestId::BoardSensorsSample)) {
        diagnostics = "Board sensor service response request id does not match.";
        return std::nullopt;
    }
    if (header.payloadBytes > kMaximumProtocolPayloadBytes || remaining != header.payloadBytes) {
        diagnostics = "Board sensor service response payload is malformed.";
        return std::nullopt;
    }

    BoardSensorsServiceResponseHeader payloadHeader;
    if (!ReadBytes(cursor, remaining, &payloadHeader, sizeof(payloadHeader))) {
        diagnostics = "Board sensor service response payload is too short.";
        return std::nullopt;
    }
    if (payloadHeader.magic != kBoardSensorsServicePayloadMagic) {
        diagnostics = "Board sensor service response payload magic does not match.";
        return std::nullopt;
    }
    if (payloadHeader.version != kFpsServiceProtocolVersion) {
        diagnostics = "Board sensor service response payload version is not supported.";
        return std::nullopt;
    }

    const bool countsAreSane = payloadHeader.requestedFanCount <= kMaximumBoardSensorCount &&
        payloadHeader.requestedTemperatureCount <= kMaximumBoardSensorCount &&
        payloadHeader.availableFanCount <= kMaximumBoardSensorCount &&
        payloadHeader.availableTemperatureCount <= kMaximumBoardSensorCount &&
        payloadHeader.fanCount <= kMaximumBoardSensorCount &&
        payloadHeader.temperatureCount <= kMaximumBoardSensorCount;
    if (!countsAreSane) {
        diagnostics = "Board sensor service response contains too many metrics.";
        return std::nullopt;
    }

    BoardVendorTelemetrySample sample;
    sample.available = (payloadHeader.flags & kBoardSensorsServiceFlagAvailable) != 0;
    if (
        !ReadString(cursor, remaining, payloadHeader.boardManufacturerBytes, sample.boardManufacturer) ||
        !ReadString(cursor, remaining, payloadHeader.boardProductBytes, sample.boardProduct) ||
        !ReadString(cursor, remaining, payloadHeader.driverLibraryBytes, sample.driverLibrary) ||
        !ReadString(cursor, remaining, payloadHeader.providerNameBytes, sample.providerName) ||
        !ReadString(cursor, remaining, payloadHeader.diagnosticsBytes, sample.diagnostics) ||
        !ReadStringVector(cursor, remaining, payloadHeader.requestedFanCount, sample.requestedFanNames) ||
        !ReadStringVector(
            cursor,
            remaining,
            payloadHeader.requestedTemperatureCount,
            sample.requestedTemperatureNames
        ) ||
        !ReadStringVector(cursor, remaining, payloadHeader.availableFanCount, sample.availableFanNames) ||
        !ReadStringVector(
            cursor,
            remaining,
            payloadHeader.availableTemperatureCount,
            sample.availableTemperatureNames
        ) ||
        !ReadMetricVector(cursor, remaining, payloadHeader.fanCount, sample.fans) ||
        !ReadMetricVector(cursor, remaining, payloadHeader.temperatureCount, sample.temperatures) ||
        remaining != 0
    ) {
        diagnostics = "Board sensor service response string payload is malformed.";
        return std::nullopt;
    }

    return sample;
}
