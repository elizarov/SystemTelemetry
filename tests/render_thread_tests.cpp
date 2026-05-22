#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dashboard_renderer/impl/render_thread.h"
#include "util/file_path.h"
#include "widget/impl/animation_primitives.h"

namespace {

const void* RenderThreadPoolTestResourceTypeToken() {
    static const int token = 0;
    return &token;
}

class RenderThreadPoolTestResource final : public RenderBitmapResource {
public:
    const void* TypeToken() const override {
        return RenderThreadPoolTestResourceTypeToken();
    }
};

RenderBitmap LiveLayerBitmap(int width, int height) {
    RenderBitmap bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.storage = RenderBitmapStorage::LiveLayer;
    bitmap.resource = std::make_shared<RenderThreadPoolTestResource>();
    return bitmap;
}

class RenderThreadTestAnimation final : public WidgetAnimation {
public:
    explicit RenderThreadTestAnimation(RenderRect dirtyBounds) : dirtyBounds_(dirtyBounds) {}

    const AnimationDataKey& Key() const override {
        return key_;
    }

    RenderRect DirtyBounds() const override {
        return dirtyBounds_;
    }

    void Draw(Renderer& renderer, const WidgetAnimationState&) const override {
        renderer.FillSolidRect(dirtyBounds_, RenderColorId::Accent);
    }

private:
    AnimationDataKey key_{"render_thread_test", {}};
    RenderRect dirtyBounds_{};
};

class RenderThreadTestRenderer final : public Renderer {
public:
    bool SetStyle(const RendererStyle&) override {
        return true;
    }

    void AttachWindow(HWND) override {}

    void Shutdown() override {}

    void SetImmediatePresent(bool) override {}

    void DiscardWindowTarget(std::string_view = {}) override {}

    bool DrawWindow(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawWindowRetained(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawWindowDirty(int, int, std::span<const RenderRect> dirtyRects, const DirtyDrawCallback& draw) override {
        dirtyWindowRects.assign(dirtyRects.begin(), dirtyRects.end());
        draw(dirtyRects);
        return true;
    }

    bool DrawOffscreen(int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    bool DrawToBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear, const DrawCallback& draw) override {
        bitmap = LiveLayerBitmap(width, height);
        draw();
        return true;
    }

    bool DrawToLiveLayerBitmap(
        RenderBitmap& bitmap, int width, int height, RenderBitmapClear clear, const DrawCallback& draw) override {
        return DrawToBitmap(bitmap, width, height, clear, draw);
    }

    bool SavePng(const FilePath&, int, int, const DrawCallback& draw) override {
        draw();
        return true;
    }

    const std::string& LastError() const override {
        return empty_;
    }

    const TextStyleMetrics& TextMetrics() const override {
        return textMetrics_;
    }

    int ScaleLogical(int value) const override {
        return value;
    }

    int MeasureTextWidth(TextStyleId, std::string_view text) const override {
        return static_cast<int>(text.size());
    }

    TextLayoutResult MeasureTextBlock(
        const RenderRect& rect, const std::string&, TextStyleId, const TextLayoutOptions&) const override {
        return TextLayoutResult{rect};
    }

    void DrawText(
        const RenderRect&, const std::string&, TextStyleId, RenderColorId, const TextLayoutOptions&) const override {}

    TextLayoutResult DrawTextBlock(
        const RenderRect& rect, const std::string&, TextStyleId, RenderColorId, const TextLayoutOptions&) override {
        return TextLayoutResult{rect};
    }

    void PushClipRect(const RenderRect& rect) override {
        pushedClipRects.push_back(rect);
    }

    void PopClipRect() override {}

    void PushTranslation(RenderPoint) override {}

    void PopTranslation() override {}

    bool DrawBitmap(const RenderBitmap&, RenderPoint) override {
        return true;
    }

    bool DrawBitmapRegion(const RenderBitmap&, const RenderRect&, RenderPoint) override {
        return true;
    }

    bool DrawBitmapRegions(const RenderBitmap&, std::span<const RenderRect>) override {
        return true;
    }

    bool DrawIcon(std::string_view, const RenderRect&) override {
        return true;
    }

    bool FillSolidRect(const RenderRect&, RenderColorId) override {
        return true;
    }

    bool FillSolidRoundedRect(const RenderRect&, int, RenderColorId) override {
        return true;
    }

    bool FillSolidEllipse(const RenderRect&, RenderColorId) override {
        return true;
    }

    bool FillSolidDiamond(const RenderRect&, RenderColorId) override {
        return true;
    }

    bool DrawSolidRect(const RenderRect&, const RenderStroke&) override {
        return true;
    }

    bool DrawSolidRoundedRect(const RenderRect&, int, const RenderStroke&) override {
        return true;
    }

    bool DrawSolidEllipse(const RenderRect&, const RenderStroke&) override {
        return true;
    }

    bool DrawSolidLine(RenderPoint, RenderPoint, const RenderStroke&) override {
        return true;
    }

    bool DrawArc(const RenderArc&, const RenderStroke&) override {
        return true;
    }

    bool DrawArcs(std::span<const RenderArc>, const RenderStroke&) override {
        return true;
    }

    bool DrawPolyline(std::span<const RenderPoint>, const RenderStroke&) override {
        return true;
    }

    bool FillPath(const RenderPath&, RenderColorId) override {
        return true;
    }

    bool FillPaths(std::span<const RenderPath>, RenderColorId) override {
        return true;
    }

    void ClearRecords() {
        pushedClipRects.clear();
        dirtyWindowRects.clear();
    }

    std::vector<RenderRect> pushedClipRects;
    std::vector<RenderRect> dirtyWindowRects;

private:
    TextStyleMetrics textMetrics_{};
    std::string empty_;
};

WidgetAnimationStatePtr TestScalarTarget() {
    ScalarFillSample sample;
    sample.valueRatio = 1.0;
    return MakeScalarFillAnimationState(sample);
}

DashboardPresentationFrame BuildClippedAnimationFrame() {
    DashboardPresentationFrame frame;
    frame.width = 100;
    frame.height = 50;
    frame.animate = true;
    frame.snapshotLayer = LiveLayerBitmap(frame.width, frame.height);
    frame.versions.surfaceVersion = 1;
    frame.versions.snapshotVersion = 1;
    frame.versions.animationGeometryVersion = 1;
    frame.versions.metricVersion = 1;
    frame.snapshotAnimations.push_back(DashboardPresentationAnimation{
        std::make_unique<RenderThreadTestAnimation>(RenderRect{10, 10, 90, 30}),
        TestScalarTarget(),
        {},
        RenderRect{20, 0, 60, 25},
    });
    return frame;
}

void ExpectRectEq(const RenderRect& actual, const RenderRect& expected) {
    EXPECT_EQ(actual.left, expected.left);
    EXPECT_EQ(actual.top, expected.top);
    EXPECT_EQ(actual.right, expected.right);
    EXPECT_EQ(actual.bottom, expected.bottom);
}

}  // namespace

TEST(DashboardLayerBitmapPool, RejectsOldSizedBitmapsAfterSurfaceSizeChanges) {
    DashboardLayerBitmapPool pool;
    pool.SetLiveLayerSize(100, 50);
    pool.ReleaseLiveLayerBitmap(LiveLayerBitmap(100, 50));

    EXPECT_FALSE(pool.AcquireLiveLayerBitmap(100, 50).Empty());

    pool.ReleaseLiveLayerBitmap(LiveLayerBitmap(100, 50));
    pool.SetLiveLayerSize(200, 80);
    pool.ReleaseLiveLayerBitmap(LiveLayerBitmap(100, 50));

    EXPECT_TRUE(pool.AcquireLiveLayerBitmap(100, 50).Empty());
    EXPECT_TRUE(pool.AcquireLiveLayerBitmap(200, 80).Empty());

    pool.ReleaseLiveLayerBitmap(LiveLayerBitmap(200, 80));

    EXPECT_FALSE(pool.AcquireLiveLayerBitmap(200, 80).Empty());
}

TEST(DashboardRenderThread, ClipsAnimationDrawsAndDirtyRectsToCommandClip) {
    DashboardRenderThread renderThread;
    RenderThreadTestRenderer renderer;
    const RenderRect expectedClip{20, 7, 60, 25};

    ASSERT_TRUE(renderThread.PresentFrameSynchronously(renderer, BuildClippedAnimationFrame()));
    ASSERT_EQ(renderer.pushedClipRects.size(), 1u);
    ExpectRectEq(renderer.pushedClipRects.front(), expectedClip);

    renderer.ClearRecords();

    ASSERT_TRUE(renderThread.PresentFrameSynchronously(renderer, BuildClippedAnimationFrame()));
    ASSERT_EQ(renderer.dirtyWindowRects.size(), 1u);
    ExpectRectEq(renderer.dirtyWindowRects.front(), expectedClip);
    ASSERT_EQ(renderer.pushedClipRects.size(), 1u);
    ExpectRectEq(renderer.pushedClipRects.front(), expectedClip);
}
