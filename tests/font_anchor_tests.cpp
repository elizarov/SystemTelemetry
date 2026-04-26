#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "config/config_parser.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_model/dashboard_overlay_state.h"
#include "telemetry/impl/collector_fake.h"
#include "util/trace.h"

namespace {

std::filesystem::path SourceConfigPath() {
    return std::filesystem::path(SYSTEMTELEMETRY_SOURCE_DIR) / "resources" / "config.ini";
}

ConfigParseContext TestConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

template <size_t... Index>
std::vector<const UiFontConfig*> FontFieldPointers(const AppConfig& config, std::index_sequence<Index...>) {
    using FontFields = std::remove_cvref_t<decltype(UiFontSetConfig::Section::fields)>;
    return {&std::tuple_element_t<Index, FontFields>::RawGet(config.layout.fonts)...};
}

std::vector<const UiFontConfig*> FontFieldPointers(const AppConfig& config) {
    using FontFields = std::remove_cvref_t<decltype(UiFontSetConfig::Section::fields)>;
    return FontFieldPointers(config, std::make_index_sequence<std::tuple_size_v<FontFields>>{});
}

bool ActiveRegionsContainFontParameter(
    const std::vector<DashboardActiveRegion>& regions, LayoutEditParameter parameter) {
    for (const DashboardActiveRegion& region : regions) {
        if (region.kind != DashboardActiveRegionKind::StaticEditAnchorHandle &&
            region.kind != DashboardActiveRegionKind::StaticEditAnchorTarget &&
            region.kind != DashboardActiveRegionKind::DynamicEditAnchorHandle &&
            region.kind != DashboardActiveRegionKind::DynamicEditAnchorTarget) {
            continue;
        }
        const auto* anchorRegion = std::get_if<const LayoutEditAnchorRegion*>(&region.payload);
        if (anchorRegion == nullptr || *anchorRegion == nullptr) {
            continue;
        }
        const auto* anchorParameter = std::get_if<LayoutEditParameter>(&(*anchorRegion)->key.subject);
        if (anchorParameter != nullptr && *anchorParameter == parameter) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(FontAnchors, BuiltInLayoutRegistersActiveRegionForEveryFontsSectionRole) {
    AppConfig config = LoadConfig(SourceConfigPath(), true, TestConfigParseContext());

    Trace trace;
    std::unique_ptr<TelemetryCollector> telemetry =
        CreateFakeTelemetryCollector(std::filesystem::current_path(), {}, nullptr, trace);
    ASSERT_NE(telemetry, nullptr);
    std::string telemetryError;
    ASSERT_TRUE(telemetry->Initialize(ExtractTelemetrySettings(config), &telemetryError)) << telemetryError;
    telemetry->UpdateSnapshot();

    DashboardRenderer renderer(trace);
    renderer.SetConfig(config);

    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;

    ASSERT_TRUE(renderer.RenderSnapshotOffscreen(telemetry->Snapshot(), overlayState)) << renderer.LastError();

    const std::vector<DashboardActiveRegion> activeRegions = renderer.CollectActiveRegions(overlayState);
    const std::vector<const UiFontConfig*> fontFields = FontFieldPointers(config);
    for (size_t index = 0; index < fontFields.size(); ++index) {
        SCOPED_TRACE(index);
        bool hasLayoutEditParameter = false;
        bool hasActiveRegion = false;
        for (int rawParameter = 0; rawParameter < static_cast<int>(LayoutEditParameter::Count); ++rawParameter) {
            const auto parameter = static_cast<LayoutEditParameter>(rawParameter);
            const std::optional<const UiFontConfig*> font = FindLayoutEditTooltipFontValue(config, parameter);
            if (!font.has_value() || *font != fontFields[index]) {
                continue;
            }
            hasLayoutEditParameter = true;
            hasActiveRegion = hasActiveRegion || ActiveRegionsContainFontParameter(activeRegions, parameter);
        }
        EXPECT_TRUE(hasLayoutEditParameter);
        EXPECT_TRUE(hasActiveRegion);
    }
}
