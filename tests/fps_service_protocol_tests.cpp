#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "telemetry/fps_service_protocol.h"

TEST(FpsServiceProtocol, RecognizesVersionedRequest) {
    const std::vector<char> request = BuildFpsServiceRequest();

    EXPECT_TRUE(IsFpsServiceRequest(request.data(), request.size()));
    EXPECT_FALSE(IsFpsServiceRequest(request.data(), request.size() - 1));
    EXPECT_FALSE(IsFpsServiceRequest("wrong", 5));
}

TEST(FpsServiceProtocol, RoundTripsAvailableSample) {
    FpsTelemetrySample sample;
    sample.available = true;
    sample.fps = 144.5;
    sample.processId = 42;
    sample.processName = "game.exe";
    sample.diagnostics = "service active";

    const std::vector<char> response = SerializeFpsServiceSample(sample);
    std::string diagnostics;
    const std::optional<FpsTelemetrySample> parsed =
        ParseFpsServiceResponse(response.data(), response.size(), diagnostics);

    ASSERT_TRUE(parsed.has_value()) << diagnostics;
    const FpsTelemetrySample& parsedSample = *parsed;
    EXPECT_TRUE(parsedSample.available);
    ASSERT_TRUE(parsedSample.fps.has_value());
    const double fps = *parsedSample.fps;
    EXPECT_DOUBLE_EQ(fps, 144.5);
    EXPECT_EQ(parsedSample.processId, 42u);
    EXPECT_EQ(parsedSample.processName, "game.exe");
    EXPECT_EQ(parsedSample.diagnostics, "service active");
    EXPECT_FALSE(parsedSample.permissionRequired);
}

TEST(FpsServiceProtocol, RoundTripsPermissionRequiredSample) {
    FpsTelemetrySample sample;
    sample.available = false;
    sample.permissionRequired = true;
    sample.diagnostics = "access denied";

    const std::vector<char> response = SerializeFpsServiceSample(sample);
    std::string diagnostics;
    const std::optional<FpsTelemetrySample> parsed =
        ParseFpsServiceResponse(response.data(), response.size(), diagnostics);

    ASSERT_TRUE(parsed.has_value()) << diagnostics;
    const FpsTelemetrySample& parsedSample = *parsed;
    EXPECT_FALSE(parsedSample.available);
    EXPECT_FALSE(parsedSample.fps.has_value());
    EXPECT_TRUE(parsedSample.permissionRequired);
    EXPECT_EQ(parsedSample.diagnostics, "access denied");
}

TEST(FpsServiceProtocol, RejectsMalformedResponse) {
    const std::vector<char> response = {1, 2, 3};
    std::string diagnostics;

    EXPECT_FALSE(ParseFpsServiceResponse(response.data(), response.size(), diagnostics).has_value());
    EXPECT_FALSE(diagnostics.empty());
}

TEST(FpsServiceProtocol, RejectsVersionMismatch) {
    FpsTelemetrySample sample;
    sample.diagnostics = "service active";
    std::vector<char> response = SerializeFpsServiceSample(sample);
    const uint32_t unsupportedVersion = 999;
    memcpy(response.data() + sizeof(uint32_t), &unsupportedVersion, sizeof(unsupportedVersion));

    std::string diagnostics;
    EXPECT_FALSE(ParseFpsServiceResponse(response.data(), response.size(), diagnostics).has_value());
    EXPECT_NE(diagnostics.find("version"), std::string::npos);
}
