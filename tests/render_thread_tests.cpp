#include <gtest/gtest.h>
#include <memory>

#include "dashboard_renderer/impl/render_thread.h"

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
