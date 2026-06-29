// Unit test for TuneNode (Phase 2.3): exposure parameter, clamping, dirty
// behaviour, and that apply() produces a valid image.

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/PreviewState.h"
#include "core/TuneNode.h"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    TuneNode node;
    CHECK(node.exposure() == 0.0f);
    CHECK(node.typeName() == QLatin1String("tune"));

    node.clearDirty();
    node.setExposure(1.5f);
    CHECK(node.exposure() == 1.5f);
    CHECK(node.isDirty()); // changing a parameter invalidates the cache

    // Setting the same value again does not re-dirty.
    node.clearDirty();
    node.setExposure(1.5f);
    CHECK(!node.isDirty());

    // Out-of-range values clamp.
    node.setExposure(100.0f);
    CHECK(node.exposure() == TuneNode::kMaxExposure);
    node.setExposure(-100.0f);
    CHECK(node.exposure() == TuneNode::kMinExposure);

    Image src = Image::black(4, 4);
    CHECK(!src.isNull());

    // ev == 0 is an identity passthrough.
    TuneNode zero;
    Image out0 = zero.apply(src);
    CHECK(out0.width() == 4 && out0.height() == 4);

    // ev != 0 returns a valid image of the same size.
    Image out1 = node.apply(src);
    CHECK(!out1.isNull());
    CHECK(out1.width() == 4 && out1.height() == 4);

    // Contrast / saturation clamp and dirty like exposure.
    node.clearDirty();
    node.setContrast(40.0f);
    CHECK(node.contrast() == 40.0f);
    CHECK(node.isDirty());
    node.setContrast(999.0f);
    CHECK(node.contrast() == TuneNode::kMaxAmount);
    node.setSaturation(-30.0f);
    CHECK(node.saturation() == -30.0f);

    // The graph accumulates preview state by walking enabled nodes.
    EditGraph graph;
    auto *t = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    t->setExposure(1.5f);
    CHECK(graph.previewState().exposure == 1.5f);

    // Contrast/saturation become factors in preview state (1 + amount/100).
    t->setContrast(50.0f);
    t->setSaturation(-100.0f);
    CHECK(std::abs(graph.previewState().contrast - 1.5f) < 1e-6f);
    CHECK(std::abs(graph.previewState().saturation - 0.0f) < 1e-6f);

    // apply() with contrast/saturation set still yields a valid same-size image.
    Image toned = t->apply(src);
    CHECK(!toned.isNull());
    CHECK(toned.width() == 4 && toned.height() == 4);

    // Vibrance clamps and dirties like the other amounts.
    node.clearDirty();
    node.setVibrance(60.0f);
    CHECK(node.vibrance() == 60.0f);
    CHECK(node.isDirty());
    node.setVibrance(999.0f);
    CHECK(node.vibrance() == TuneNode::kMaxAmount);

    // Vibrance enters preview state as an additive amount (vibrance/100).
    {
        EditGraph g2;
        auto *tv = static_cast<TuneNode *>(g2.addNode(std::make_unique<TuneNode>()));
        tv->setVibrance(50.0f);
        CHECK(std::abs(g2.previewState().vibrance - 0.5f) < 1e-6f);
    }

    // Vibrance boosts a low-saturation pixel more (relatively) than an already-
    // saturated one: muted red grows proportionally more than vivid red.
    {
        const uint8_t px[8] = {140, 120, 120, 255,  // low saturation
                               220, 40, 40, 255};   // high saturation
        Image two = Image::fromInterleaved(px, 2, 1, 4);
        CHECK(!two.isNull());
        TuneNode vib;
        vib.setVibrance(100.0f);
        QImage q = vib.apply(two).toQImage();
        const auto satOf = [](const QColor &c) {
            return std::max({c.red(), c.green(), c.blue()})
                 - std::min({c.red(), c.green(), c.blue()});
        };
        const QColor inLow(140, 120, 120), inHigh(220, 40, 40);
        const QColor outLow = q.pixelColor(0, 0), outHigh = q.pixelColor(1, 0);
        CHECK(satOf(outLow) > satOf(inLow)); // muted pixel gained saturation
        const double rLow = double(satOf(outLow)) / satOf(inLow);
        const double rHigh = double(satOf(outHigh)) / satOf(inHigh);
        CHECK(rLow > rHigh); // muted colour pushed harder than the vivid one
    }

    // A second enabled tune node sums (EV stops add).
    auto *t2 = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    t2->setExposure(0.5f);
    CHECK(graph.previewState().exposure == 2.0f);

    // Disabled nodes don't contribute.
    t->setEnabled(false);
    CHECK(graph.previewState().exposure == 0.5f);

    // --- White balance (v2: Kelvin / linear-light 3x3) ---------------------
    {
        // Default: no camera profile → sRGB model, neutral at 6500 K (= as-shot),
        // so the WB matrix is the identity and preview state is unchanged.
        TuneNode wb;
        CHECK(wb.kelvin() == TuneNode::kDefaultKelvin);
        CHECK(wb.asShotKelvin() == TuneNode::kDefaultKelvin);
        double W[9];
        wb.whiteBalanceMatrix(W);
        CHECK(std::abs(W[0] - 1) < 1e-6 && std::abs(W[4] - 1) < 1e-6
              && std::abs(W[8] - 1) < 1e-6);

        // Kelvin clamps + dirties.
        wb.clearDirty();
        wb.setKelvin(5000.0f);
        CHECK(wb.kelvin() == 5000.0f && wb.isDirty());
        wb.setKelvin(99999.0f);
        CHECK(wb.kelvin() == TuneNode::kMaxKelvin);
        wb.setTint(-40.0f);
        CHECK(wb.tint() == -40.0f);

        // Preview state carries the WB matrix; warmer (higher K) → red gain up,
        // blue gain down on the diagonal (sRGB path).
        EditGraph g2;
        auto *tw = static_cast<TuneNode *>(g2.addNode(std::make_unique<TuneNode>()));
        tw->setKelvin(9000.0f);
        CHECK(g2.previewState().wb00 > 1.0f);
        CHECK(g2.previewState().wb22 < 1.0f);

        // Tint: +tint (magenta) lowers the green gain.
        EditGraph g3;
        auto *tt = static_cast<TuneNode *>(g3.addNode(std::make_unique<TuneNode>()));
        tt->setTint(80.0f);
        CHECK(g3.previewState().wb11 < 1.0f);

        // apply(): a warm shift (higher K) raises red and lowers blue on grey.
        std::vector<uint8_t> px(4 * 4 * 4, 255);
        for (int i = 0; i < 4 * 4; ++i)
            px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 128;
        Image grey = Image::fromInterleaved(px.data(), 4, 4, 4);
        TuneNode warm;
        warm.setKelvin(9000.0f);
        const QColor c = warm.apply(grey).toQImage().pixelColor(0, 0);
        CHECK(c.red() > 128 && c.blue() < 128 && c.red() > c.blue());

        // Camera profile: seeds the slider to the as-shot temperature and the
        // matrix is identity there (image unchanged at default).
        const double camToRgb[9] = {1.6, -0.5, -0.1, -0.2, 1.4, -0.2, 0.0, -0.4, 1.4};
        const double xyzToCam[9] = {0.8, 0.1, 0.05, 0.2, 0.9, -0.1, 0.0, 0.1, 1.1};
        const double asShotMul[3] = {2.0, 1.0, 1.5};
        TuneNode cam;
        cam.setCameraProfile(camToRgb, xyzToCam, asShotMul, /*seedKelvin=*/true);
        CHECK(cam.kelvin() == cam.asShotKelvin());
        cam.whiteBalanceMatrix(W);
        CHECK(std::abs(W[0] - 1) < 1e-6 && std::abs(W[4] - 1) < 1e-6
              && std::abs(W[8] - 1) < 1e-6);
        // seedKelvin=false keeps the current temperature.
        cam.setKelvin(4200.0f);
        cam.setCameraProfile(camToRgb, xyzToCam, asShotMul, /*seedKelvin=*/false);
        CHECK(cam.kelvin() == 4200.0f);
    }

    ImageBuffer::shutdownLibrary();
    std::puts("tune_node_test: OK");
    return 0;
}
