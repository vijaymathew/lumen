// Unit test for the tone curve: identity, monotone interpolation, LUT, the
// CurvesNode preview-LUT composition, and apply() on a real image.

#include "core/Curve.h"
#include "core/CurvesNode.h"
#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"

#include <array>
#include <cstdio>
#include <memory>

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

    // Identity curve: LUT[i] == i.
    Curve identity;
    CHECK(identity.isIdentity());
    const auto idLut = identity.buildLut();
    CHECK(idLut[0] == 0);
    CHECK(idLut[128] == 128);
    CHECK(idLut[255] == 255);

    // A brightening curve through (0.5, 0.7): midtones lift, endpoints fixed,
    // and the mapping stays monotonic.
    Curve bright;
    bright.setPoints({{0.0, 0.0}, {0.5, 0.7}, {1.0, 1.0}});
    CHECK(!bright.isIdentity());
    const auto lut = bright.buildLut();
    CHECK(lut[0] == 0);
    CHECK(lut[255] == 255);
    CHECK(lut[128] > 128); // midtone lifted
    for (int i = 1; i < 256; ++i)
        CHECK(lut[i] >= lut[i - 1]); // monotonic

    // Preview LUT composition through the graph.
    EditGraph graph;
    auto *node = static_cast<CurvesNode *>(graph.addNode(std::make_unique<CurvesNode>()));
    // No curve yet -> identity preview LUT.
    auto pl = graph.previewLut();
    CHECK(pl[100] == 100);
    node->setCurve(bright);
    pl = graph.previewLut();
    CHECK(pl[128] > 128);

    // Disabled node contributes nothing.
    node->setEnabled(false);
    pl = graph.previewLut();
    CHECK(pl[128] == 128);
    node->setEnabled(true);

    // apply() yields a valid same-size image; identity curve is a passthrough.
    Image src = Image::black(4, 4);
    CHECK(!src.isNull());
    CurvesNode passthrough;
    CHECK(passthrough.curve().isIdentity());
    Image same = passthrough.apply(src);
    CHECK(same.width() == 4 && same.height() == 4);
    Image out = node->apply(src);
    CHECK(!out.isNull());
    CHECK(out.width() == 4 && out.height() == 4);

    ImageBuffer::shutdownLibrary();
    std::puts("curves_test: OK");
    return 0;
}
