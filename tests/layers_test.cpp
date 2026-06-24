// Unit test for layered compositing: a masked adjustment layer over a Base
// layer applies inside its mask and leaves the rest of the image untouched.

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/Layer.h"
#include "core/TuneNode.h"

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

static bool near8(int a, int b) { return std::abs(a - b) <= 3; }

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    const int w = 64, h = 64;
    std::vector<uint8_t> grey(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        grey[i * 4 + 0] = grey[i * 4 + 1] = grey[i * 4 + 2] = 128;
        grey[i * 4 + 3] = 255;
    }
    Image src = Image::fromInterleaved(grey.data(), w, h, 4);
    CHECK(!src.isNull());

    EditGraph graph;
    graph.setSource(src);

    // Base layer with no adjustments → passthrough, mid-grey everywhere.
    CHECK(near8(graph.result().toQImage().pixelColor(w / 2, h / 2).red(), 128));
    CHECK(near8(graph.result().toQImage().pixelColor(1, 1).red(), 128));

    // Add a layer: +2 EV through a centred radial mask.
    Layer &layer = graph.addLayer(QStringLiteral("Spot"));
    auto *tune = static_cast<TuneNode *>(layer.addNode(std::make_unique<TuneNode>()));
    tune->setExposure(2.0f);
    MaskSpec m;
    m.type = MaskSpec::Radial;
    m.center = {0.5, 0.5};
    m.radiusX = m.radiusY = 0.25f;
    m.feather = 0.05f;
    layer.setMask(m);

    QImage out = graph.result().toQImage();
    CHECK(out.pixelColor(w / 2, h / 2).red() > 220); // centre brightened (inside mask)
    CHECK(near8(out.pixelColor(1, 1).red(), 128));    // corner untouched (outside)

    // Opacity halves the effect at the centre.
    layer.setOpacity(0.5f);
    const int lit = graph.result().toQImage().pixelColor(w / 2, h / 2).red();
    CHECK(lit > 160 && lit < 215); // ~mix(128, ~240, 0.5)

    // Disabling the layer falls back to the Base passthrough.
    layer.setEnabled(false);
    CHECK(near8(graph.result().toQImage().pixelColor(w / 2, h / 2).red(), 128));

    // A luminosity-masked layer (the dissolved "selective" path): mid-grey has
    // luminance 0.5. A [0.3,0.7] range includes it → +2 EV brightens; inverting
    // that range excludes it → no change. (Replaces the old SelectiveNode test.)
    Layer &lum = graph.addLayer(QStringLiteral("Lum"));
    auto *lumTune = static_cast<TuneNode *>(lum.addNode(std::make_unique<TuneNode>()));
    lumTune->setExposure(2.0f);
    MaskSpec lm;
    lm.type = MaskSpec::Luminosity;
    lm.low = 0.3f;
    lm.high = 0.7f;
    lm.feather = 0.05f;
    lum.setMask(lm);
    CHECK(graph.result().toQImage().pixelColor(w / 2, h / 2).red() > 200);

    // Range that excludes mid-grey (shadows only) → untouched.
    lm.low = 0.0f;
    lm.high = 0.4f;
    lum.setMask(lm);
    CHECK(near8(graph.result().toQImage().pixelColor(w / 2, h / 2).red(), 128));

    // Inverting the midtone range also excludes mid-grey → untouched.
    lm.low = 0.3f;
    lm.high = 0.7f;
    lm.invert = true;
    lum.setMask(lm);
    CHECK(near8(graph.result().toQImage().pixelColor(w / 2, h / 2).red(), 128));

    ImageBuffer::shutdownLibrary();
    std::puts("layers_test: OK");
    return 0;
}
