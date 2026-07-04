// Unit test for StructureNode (local contrast / "Structure"): passthrough when
// off, no effect on flat regions, positive amounts add local-contrast overshoot
// at edges (and negative amounts don't), and save/restore round-trips.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/StructureNode.h"

#include <QColor>
#include <QImage>

#include <cstdint>
#include <cstdio>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// A solid grey (4-band) image.
static Image solid(int v, int w = 96, int h = 96)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = static_cast<uint8_t>(v);
        px[i * 4 + 3] = 255;
    }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

// A vertical step edge: left half `lo`, right half `hi` (grey, 4-band).
static Image edge(int lo, int hi, int w = 96, int h = 96)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t v = static_cast<uint8_t>(x < w / 2 ? lo : hi);
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

static void lumaRange(const QImage &q, int &lo, int &hi)
{
    lo = 255;
    hi = 0;
    for (int y = 0; y < q.height(); ++y)
        for (int x = 0; x < q.width(); ++x) {
            const int r = q.pixelColor(x, y).red();
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    StructureNode node;

    // Disabled → exact passthrough.
    {
        Image grey = solid(128);
        QImage q = node.apply(grey).toQImage();
        CHECK(q.width() == 96 && q.height() == 96);
        CHECK(q.pixelColor(10, 10).red() == 128);
    }

    // Enabled on a FLAT image → no change: the high-pass of a flat region is zero,
    // so structure touches texture, not smooth areas.
    {
        StructureNode::Values v;
        v.enabled = true;
        v.amount = 100.0f;
        v.radius = 12.0f;
        node.setValues(v);
        QImage q = node.apply(solid(128)).toQImage();
        int lo = 0, hi = 0;
        lumaRange(q, lo, hi);
        CHECK(lo >= 127 && hi <= 129); // essentially unchanged
    }

    // Positive amount on an EDGE → local-contrast overshoot pushes values beyond
    // the original [100,156] range (classic unsharp halo, the structure signal).
    {
        StructureNode::Values v;
        v.enabled = true;
        v.amount = 100.0f;
        v.radius = 12.0f;
        node.setValues(v);
        QImage q = node.apply(edge(100, 156)).toQImage();
        int lo = 0, hi = 0;
        lumaRange(q, lo, hi);
        CHECK(lo < 100); // dark side undershoots
        CHECK(hi > 156); // bright side overshoots
    }

    // Negative amount on the same edge → softens: it moves values toward the blur,
    // so it never overshoots the original range the way a positive amount does.
    {
        StructureNode::Values v;
        v.enabled = true;
        v.amount = -80.0f;
        v.radius = 12.0f;
        node.setValues(v);
        const Image original = edge(100, 156);
        QImage q = node.apply(original).toQImage();
        int lo = 0, hi = 0;
        lumaRange(q, lo, hi);
        CHECK(lo >= 100 && hi <= 156); // no overshoot
        // ...but it did change the transition (some pixel differs from the original).
        QImage o = original.toQImage();
        bool changed = false;
        for (int y = 0; y < o.height() && !changed; ++y)
            for (int x = 0; x < o.width() && !changed; ++x)
                if (q.pixelColor(x, y).red() != o.pixelColor(x, y).red())
                    changed = true;
        CHECK(changed);
    }

    // Save/restore round-trips the values.
    {
        StructureNode::Values v;
        v.enabled = true;
        v.amount = -42.0f;
        v.radius = 20.0f;
        node.setValues(v);
        StructureNode restored;
        restored.restoreState(node.saveState());
        CHECK(restored.values().enabled == true);
        CHECK(restored.values().amount == -42.0f);
        CHECK(restored.values().radius == 20.0f);
        CHECK(restored.typeName() == QStringLiteral("structure"));
    }

    std::printf("OK: structure\n");
    return 0;
}
