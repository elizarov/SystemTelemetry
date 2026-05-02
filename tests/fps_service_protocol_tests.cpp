#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "telemetry/fps/fps_service_protocol.h"

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
    EXPECT_TRUE(parsed->available);
    ASSERT_TRUE(parsed->fps.has_value());
    EXPECT_DOUBLE_EQ(*parsed->fps, 144.5);
    EXPECT_EQ(parsed->processId, 42u);
    EXPECT_EQ(parsed->processName, "game.exe");
    EXPECT_EQ(parsed->diagnostics, "service active");
    EXPECT_FALSE(parsed->permissionRequired);
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
    EXPECT_FALSE(parsed->available);
    EXPECT_FALSE(parsed->fps.has_value());
    EXPECT_TRUE(parsed->permissionRequired);
    EXPECT_EQ(parsed->diagnostics, "access denied");
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
