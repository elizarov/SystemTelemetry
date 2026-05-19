#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "config/config_telemetry.h"
#include "config/config_parser.h"
#include "config/config_runtime_fields.h"
#include "dashboard_renderer/dashboard_renderer.h"
#include "layout_edit/layout_edit_parameter_edit.h"
#include "layout_model/dashboard_overlay_state.h"
#include "telemetry/impl/collector_fake.h"
#include "util/file_path.h"
#include "util/trace.h"

namespace {

FilePath SourceConfigPath() {
    return FilePath(CASEDASH_SOURCE_DIR) / "resources" / "config.ini";
}

ConfigParseContext TestConfigParseContext() {
    return ConfigParseContext{TelemetryMetricCatalog()};
}

std::vector<const UiFontConfig*> FontFieldPointers(const AppConfig& config) {
    std::vector<const UiFontConfig*> fields;
    const RuntimeConfigSectionDescriptor* fontsSection = FindRuntimeConfigSectionByName("fonts");
    EXPECT_NE(fontsSection, nullptr);
    if (fontsSection == nullptr) {
        return fields;
    }
    for (const RuntimeConfigFieldDescriptor& field : RuntimeConfigFields(*fontsSection)) {
        if (field.kind != RuntimeConfigFieldValueKind::FontSpec) {
            continue;
        }
        fields.push_back(
            reinterpret_cast<const UiFontConfig*>(reinterpret_cast<const char*>(&config.layout.fonts) + field.offset));
    }
    return fields;
}

bool ActiveRegionsContainFontParameter(const LayoutEditActiveRegions& regions, LayoutEditParameter parameter) {
    for (const LayoutEditActiveRegion& region : regions) {
        if (region.kind != LayoutEditActiveRegionKind::StaticEditAnchorHandle &&
            region.kind != LayoutEditActiveRegionKind::StaticEditAnchorTarget &&
            region.kind != LayoutEditActiveRegionKind::DynamicEditAnchorHandle &&
            region.kind != LayoutEditActiveRegionKind::DynamicEditAnchorTarget) {
            continue;
        }
        const auto* anchorRegion = LayoutEditActiveRegionPayloadAs<LayoutEditAnchorRegion>(region);
        if (anchorRegion == nullptr) {
            continue;
        }
        const auto* anchorParameter = std::get_if<LayoutEditParameter>(&anchorRegion->key.subject);
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
        CreateFakeTelemetryCollector(CurrentDirectoryPath(), {}, nullptr, false, trace);
    ASSERT_NE(telemetry, nullptr);
    std::string telemetryError;
    ASSERT_TRUE(telemetry->Initialize(ExtractTelemetrySettings(config), &telemetryError)) << telemetryError;
    telemetry->UpdateSnapshot();

    DashboardRenderer renderer(trace);
    renderer.SetConfig(config);

    DashboardOverlayState overlayState;
    overlayState.showLayoutEditGuides = true;

    ASSERT_TRUE(renderer.RenderSnapshotOffscreen(telemetry->Snapshot(), overlayState)) << renderer.LastError();

    const LayoutEditActiveRegions activeRegions = renderer.CollectLayoutEditActiveRegions(overlayState);
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
