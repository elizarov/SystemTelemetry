#pragma once

#include <string>
#include <vector>

#include "dashboard_renderer/dashboard_renderer.h"

std::vector<LayoutGuideSheetCardSummary> CollectLayoutGuideSheetCardSummaries(const DashboardRenderer& renderer);
bool SaveLayoutGuideSheetSurfacePng(
    DashboardRenderer& renderer, const FilePath& imagePath, int width, int height, Renderer::DrawCallback draw);
bool RenderLayoutGuideSheetSurfaceOffscreen(
    DashboardRenderer& renderer, int width, int height, Renderer::DrawCallback draw);
void BeginLayoutGuideSheetDynamicArtifacts(DashboardRenderer& renderer, const DashboardOverlayState& overlayState);
void ResolveLayoutGuideSheetDynamicArtifactCollisions(DashboardRenderer& renderer);
void EndLayoutGuideSheetDynamicArtifacts(DashboardRenderer& renderer);
void DrawLayoutGuideSheetCard(DashboardRenderer& renderer,
    const std::string& cardId,
    const RenderRect& sourceRect,
    const RenderRect& destRect,
    const MetricSource& metrics);
void DrawLayoutGuideSheetOverlay(DashboardRenderer& renderer,
    const DashboardOverlayState& overlayState,
    const RenderRect& sourceRect,
    const RenderRect& destRect,
    const MetricSource& metrics);
LayoutGuideSheetCardChromeArtifacts BuildLayoutGuideSheetCardChromeArtifacts(DashboardRenderer& renderer,
    const std::string& cardId,
    const RenderRect& rect,
    const MetricSource* metrics,
    bool suppressTitle = false);
