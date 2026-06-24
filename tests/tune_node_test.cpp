// Unit test for TuneNode (Phase 2.3): exposure parameter, clamping, dirty
// behaviour, and that apply() produces a valid image.

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/PreviewState.h"
#include "core/TuneNode.h"

#include <QColor>

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

    // A second enabled tune node sums (EV stops add).
    auto *t2 = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    t2->setExposure(0.5f);
    CHECK(graph.previewState().exposure == 2.0f);

    // Disabled nodes don't contribute.
    t->setEnabled(false);
    CHECK(graph.previewState().exposure == 0.5f);

    // --- White balance -----------------------------------------------------
    {
        float r, g, b;
        TuneNode::wbGains(0, 0, r, g, b); // neutral → unit gains
        CHECK(std::abs(r - 1) < 1e-6f && std::abs(g - 1) < 1e-6f && std::abs(b - 1) < 1e-6f);
        TuneNode::wbGains(100, 0, r, g, b); // warm: R up, B down
        CHECK(r > 1.0f && b < 1.0f);
        TuneNode::wbGains(0, 100, r, g, b); // magenta: G down
        CHECK(g < 1.0f);

        TuneNode wb;
        wb.clearDirty();
        wb.setTemperature(50.0f);
        CHECK(wb.temperature() == 50.0f && wb.isDirty());
        wb.setTemperature(999.0f);
        CHECK(wb.temperature() == TuneNode::kMaxAmount);
        wb.setTint(-40.0f);
        CHECK(wb.tint() == -40.0f);

        // Preview state carries the gains (warm → wbR > 1, wbB < 1).
        EditGraph g2;
        auto *tw = static_cast<TuneNode *>(g2.addNode(std::make_unique<TuneNode>()));
        tw->setTemperature(100.0f);
        CHECK(g2.previewState().wbR > 1.0f);
        CHECK(g2.previewState().wbB < 1.0f);

        // apply(): a warm shift raises red and lowers blue on a grey patch.
        std::vector<uint8_t> px(4 * 4 * 4, 255);
        for (int i = 0; i < 4 * 4; ++i) {
            px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 128;
        }
        Image grey = Image::fromInterleaved(px.data(), 4, 4, 4);
        TuneNode warm;
        warm.setTemperature(100.0f);
        const QColor c = warm.apply(grey).toQImage().pixelColor(0, 0);
        CHECK(c.red() > 140 && c.blue() < 115 && c.red() > c.blue());
    }

    ImageBuffer::shutdownLibrary();
    std::puts("tune_node_test: OK");
    return 0;
}
