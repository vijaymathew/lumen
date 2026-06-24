// Unit test for SharpenNode: passthrough when off, a flat field is left
// essentially untouched, a hard edge gains overshoot (the unsharp signature),
// and the parameters round-trip through save/restore.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/SharpenNode.h"

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

static Image solid(int v, int w = 16, int h = 16)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = static_cast<uint8_t>(v);
        px[i * 4 + 3] = 255;
    }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

// Vertical edge: left half = lo, right half = hi.
static Image edge(int lo, int hi, int w = 32, int h = 16)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            const uint8_t v = static_cast<uint8_t>(x < w / 2 ? lo : hi);
            px[i + 0] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

static void range(const QImage &q, int &mn, int &mx)
{
    mn = 255;
    mx = 0;
    for (int y = 0; y < q.height(); ++y)
        for (int x = 0; x < q.width(); ++x) {
            const int r = q.pixelColor(x, y).red();
            mn = std::min(mn, r);
            mx = std::max(mx, r);
        }
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Disabled → passthrough.
    {
        SharpenNode node; // enabled defaults to false
        Image in = edge(80, 180);
        Image out = node.apply(in);
        CHECK(out.toQImage().pixelColor(0, 0).red() == 80);
    }

    SharpenNode node;
    SharpenNode::Values v;
    v.enabled = true;
    v.amount = 100.0f;
    v.radius = 1.5f;
    node.setValues(v);

    // Flat field: no edges → essentially unchanged, dims/format preserved.
    {
        Image out = node.apply(solid(128));
        CHECK(!out.isNull());
        CHECK(out.width() == 16 && out.height() == 16);
        int mn, mx;
        range(out.toQImage(), mn, mx);
        CHECK(mn >= 124 && mx <= 132); // flat stays flat (m1 = 0)
    }

    // Hard edge: unsharp overshoot pushes values beyond the original [80,180].
    {
        Image in = edge(80, 180);
        Image out = node.apply(in);
        CHECK(!out.isNull());
        int mn, mx;
        range(out.toQImage(), mn, mx);
        CHECK(mx > 182 || mn < 78); // overshoot / undershoot at the edge
    }

    // Serialise round-trip.
    {
        SharpenNode a;
        SharpenNode::Values av;
        av.enabled = true;
        av.amount = 73.0f;
        av.radius = 2.25f;
        a.setValues(av);
        SharpenNode b;
        b.restoreState(a.saveState());
        CHECK(b.values() == av);
    }

    ImageBuffer::shutdownLibrary();
    std::puts("sharpen_test: OK");
    return 0;
}
