// Unit test for RetouchNode: painted patches take their colour, unpainted areas
// are untouched, opacity blends, patches composite in order, state round-trips.

#include "core/ImageBuffer.h"
#include "core/RetouchNode.h"
#include "core/SelectiveMask.h"

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

namespace {
// Left half painted (mask 1), right half untouched (0).
MaskBuffer leftHalf(int w, int h)
{
    MaskBuffer m;
    m.width = w;
    m.height = h;
    m.data.assign(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w / 2; ++x)
            m.data[static_cast<size_t>(y) * w + x] = 1.0f;
    return m;
}

std::vector<uint8_t> solid(int w, int h, uint8_t v)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, v);
    for (size_t i = 3; i < px.size(); i += 4)
        px[i] = 255;
    return px;
}
} // namespace

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }
    const int w = 16, h = 8;
    const size_t left = 0, right = static_cast<size_t>(w - 1) * 4;

    // Empty node is a no-op.
    {
        RetouchNode n;
        CHECK(!n.hasEffect());
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 128);
    }

    // Painted half takes the colour; the rest and alpha are untouched.
    {
        RetouchNode n;
        n.setPatch(0, 200, 100, 50, leftHalf(w, h));
        CHECK(n.hasEffect());
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 200 && px[left + 1] == 100 && px[left + 2] == 50);
        CHECK(px[left + 3] == 255);
        CHECK(px[right] == 128 && px[right + 1] == 128 && px[right + 2] == 128);
    }

    // Opacity blends towards the colour.
    {
        RetouchNode n;
        n.setPatch(0, 200, 200, 200, leftHalf(w, h));
        n.setOpacity(50);
        std::vector<uint8_t> px = solid(w, h, 100);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 150);
        CHECK(px[right] == 100);
    }

    // Later patches composite over earlier ones; an empty mask removes a patch.
    {
        RetouchNode n;
        n.setPatch(0, 255, 0, 0, leftHalf(w, h));
        n.setPatch(1, 0, 0, 255, leftHalf(w, h));
        CHECK(n.patches().size() == 2);
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 0 && px[left + 2] == 255);
        n.setPatch(1, 0, 0, 0, MaskBuffer());
        CHECK(n.patches().size() == 1);
    }

    // Mask at working resolution is resampled to the image size.
    {
        RetouchNode n;
        n.setPatch(0, 10, 20, 30, leftHalf(4, 2));
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 10);
        CHECK(px[right] == 128);
    }

    // State round-trips; apply() through libvips runs.
    {
        RetouchNode n;
        n.setPatch(0, 1, 2, 3, leftHalf(w, h));
        n.setPatch(1, 4, 5, 6, leftHalf(w, h));
        n.setOpacity(70);
        RetouchNode m;
        m.restoreState(n.saveState());
        CHECK(m.opacity() == 70);
        CHECK(m.patches().size() == 2);
        CHECK(m.patches()[1].r == 4 && m.patches()[1].g == 5 && m.patches()[1].b == 6);
        CHECK(m.patches()[0].mask.width == w);

        const std::vector<uint8_t> src = solid(w, h, 128);
        const Image out = n.apply(Image::fromInterleaved(src.data(), w, h, 4));
        CHECK(!out.isNull());
        const QImage q = out.toQImage();
        CHECK(q.width() == w && q.height() == h);
    }

    std::puts("retouch_test OK");
    return 0;
}
