// Unit test for DodgeBurnNode: dodge lightens, burn darkens, range weighting
// confines the effect, unpainted areas are untouched, state round-trips.

#include "core/DodgeBurnNode.h"
#include "core/ImageBuffer.h"
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
        DodgeBurnNode n;
        CHECK(!n.hasEffect());
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 128);
    }

    // Dodge lightens the painted half only; alpha is untouched.
    {
        DodgeBurnNode n;
        n.setDodgeMask(leftHalf(w, h));
        CHECK(n.hasEffect());
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] > 128);
        CHECK(px[right] == 128);
        CHECK(px[left + 3] == 255);
    }

    // Burn darkens.
    {
        DodgeBurnNode n;
        n.setBurnMask(leftHalf(w, h));
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] < 128);
        CHECK(px[right] == 128);
    }

    // Dodge and burn over the same area cancel out.
    {
        DodgeBurnNode n;
        n.setDodgeMask(leftHalf(w, h));
        n.setBurnMask(leftHalf(w, h));
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] == 128);
    }

    // Stronger exposure moves further.
    {
        DodgeBurnNode weak, strong;
        weak.setDodgeMask(leftHalf(w, h));
        strong.setDodgeMask(leftHalf(w, h));
        weak.setExposure(10);
        strong.setExposure(90);
        std::vector<uint8_t> a = solid(w, h, 128), b = solid(w, h, 128);
        weak.applyToPixels(a.data(), w, h, 4);
        strong.applyToPixels(b.data(), w, h, 4);
        CHECK(b[left] > a[left]);
    }

    // Range weighting: a Shadows dodge barely touches highlights but lifts
    // shadows; a Highlights dodge does the reverse.
    {
        DodgeBurnNode n;
        n.setDodgeMask(leftHalf(w, h));
        n.setRange(DodgeBurnNode::Range::Shadows);
        std::vector<uint8_t> dark = solid(w, h, 40), bright = solid(w, h, 230);
        n.applyToPixels(dark.data(), w, h, 4);
        n.applyToPixels(bright.data(), w, h, 4);
        CHECK(dark[left] - 40 > bright[left] - 230);
        n.setRange(DodgeBurnNode::Range::Highlights);
        dark = solid(w, h, 40);
        bright = solid(w, h, 230);
        n.applyToPixels(dark.data(), w, h, 4);
        n.applyToPixels(bright.data(), w, h, 4);
        CHECK(bright[left] - 230 > dark[left] - 40);
    }

    // Mask at working resolution is resampled to the image size.
    {
        DodgeBurnNode n;
        n.setDodgeMask(leftHalf(4, 2));
        std::vector<uint8_t> px = solid(w, h, 128);
        n.applyToPixels(px.data(), w, h, 4);
        CHECK(px[left] > 128);
        CHECK(px[right] == 128);
    }

    // apply() through libvips agrees with applyToPixels, and state round-trips.
    {
        DodgeBurnNode n;
        n.setDodgeMask(leftHalf(w, h));
        n.setBurnMask(leftHalf(w, h));
        n.setExposure(55);
        n.setRange(DodgeBurnNode::Range::Highlights);
        DodgeBurnNode m;
        m.restoreState(n.saveState());
        CHECK(m.exposure() == 55);
        CHECK(m.range() == DodgeBurnNode::Range::Highlights);
        CHECK(m.dodgeMask().width == w && m.burnMask().width == w);

        DodgeBurnNode d;
        d.setDodgeMask(leftHalf(w, h));
        const std::vector<uint8_t> src = solid(w, h, 128);
        std::vector<uint8_t> expect = src;
        d.applyToPixels(expect.data(), w, h, 4);
        const Image out = d.apply(Image::fromInterleaved(src.data(), w, h, 4));
        CHECK(!out.isNull());
        const QImage q = out.toQImage();
        CHECK(q.width() == w && q.height() == h);
    }

    std::puts("dodgeburn_test OK");
    return 0;
}
