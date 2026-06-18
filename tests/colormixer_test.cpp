// Unit test for ColorMixerNode (per-color HSL / colour mixer): neutral
// passthrough, per-band saturation / luminance / hue on a red patch, that
// neutral greys are untouched, the resolved preview state, and save/restore.

#include "core/ColorMixerNode.h"
#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/PreviewState.h"

#include <QColor>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// A flat 4×4 patch of a single colour (RGBA8, opaque).
static Image colorPatch(int r, int g, int b)
{
    std::vector<uint8_t> px(4 * 4 * 4, 255);
    for (int i = 0; i < 4 * 4; ++i) {
        px[i * 4 + 0] = static_cast<uint8_t>(r);
        px[i * 4 + 1] = static_cast<uint8_t>(g);
        px[i * 4 + 2] = static_cast<uint8_t>(b);
    }
    return Image::fromInterleaved(px.data(), 4, 4, 4);
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // A saturated red (hue ≈ 0°, the Red band anchor) and a neutral grey.
    const int kR = 200, kG = 30, kB = 30;

    // Neutral node is a passthrough (on colour and on grey).
    {
        ColorMixerNode n;
        const QColor c = n.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0);
        CHECK(c.red() == kR && c.green() == kG && c.blue() == kB);
        const QColor gcol = n.apply(colorPatch(128, 128, 128)).toQImage().pixelColor(0, 0);
        CHECK(gcol.red() == 128 && gcol.green() == 128 && gcol.blue() == 128);
    }

    // Red-band saturation: −100 desaturates (min channels rise toward the max),
    // +100 saturates (min channels drop).
    {
        ColorMixerNode down;
        ColorMixerValues vd;
        vd.sat[0] = -100.0f; // Red band
        down.setValues(vd);
        const QColor c = down.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0);
        CHECK(c.green() > kG && c.blue() > kB);

        ColorMixerNode up;
        ColorMixerValues vu;
        vu.sat[0] = 100.0f;
        up.setValues(vu);
        const QColor c2 = up.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0);
        CHECK(c2.green() < kG && c2.blue() < kB);
    }

    // Red-band luminance: −100 darkens, +100 brightens the red patch.
    {
        ColorMixerNode dark;
        ColorMixerValues vd;
        vd.lum[0] = -100.0f;
        dark.setValues(vd);
        CHECK(dark.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0).red() < kR);

        ColorMixerNode bright;
        ColorMixerValues vb;
        vb.lum[0] = 100.0f;
        bright.setValues(vb);
        CHECK(bright.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0).red() > kR);
    }

    // Red-band hue shift (+) rotates red → orange, raising the green channel.
    {
        ColorMixerNode n;
        ColorMixerValues v;
        v.hue[0] = 100.0f;
        n.setValues(v);
        const QColor c = n.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0);
        CHECK(c.green() > kG);
    }

    // A neutral grey is untouched even with a strong Red-band adjustment, because
    // the effect is weighted by the pixel's saturation (0 for grey).
    {
        ColorMixerNode n;
        ColorMixerValues v;
        v.lum[0] = -100.0f;
        v.sat[0] = 100.0f;
        n.setValues(v);
        const QColor c = n.apply(colorPatch(128, 128, 128)).toQImage().pixelColor(0, 0);
        CHECK(c.red() == 128 && c.green() == 128 && c.blue() == 128);
    }

    // Only the addressed band acts: a Blue-band adjustment leaves the red patch
    // essentially unchanged (±1, from the one HSV round-trip's quantisation).
    {
        ColorMixerNode n;
        ColorMixerValues v;
        v.sat[5] = -100.0f; // Blue band (index 5)
        n.setValues(v);
        const QColor c = n.apply(colorPatch(kR, kG, kB)).toQImage().pixelColor(0, 0);
        const auto near = [](int a, int b) { return std::abs(a - b) <= 1; };
        CHECK(near(c.red(), kR) && near(c.green(), kG) && near(c.blue(), kB));
    }

    // Preview state: neutral leaves colorMixEnabled 0; an active band sets it 1
    // and carries the /100 amount in the matching slot.
    {
        EditGraph g;
        auto *n = static_cast<ColorMixerNode *>(
            g.addNode(std::make_unique<ColorMixerNode>()));
        CHECK(g.previewState().colorMixEnabled == 0.0f);
        ColorMixerValues v;
        v.sat[0] = 50.0f;
        n->setValues(v);
        const PreviewState ps = g.previewState();
        CHECK(ps.colorMixEnabled == 1.0f);
        CHECK(ps.mixSat0 == 0.5f);
    }

    // Save / restore round-trip.
    {
        ColorMixerNode a;
        ColorMixerValues v;
        v.hue[2] = 40.0f;
        v.sat[4] = -60.0f;
        v.lum[7] = 25.0f;
        a.setValues(v);
        ColorMixerNode b;
        b.restoreState(a.saveState());
        CHECK(b.values() == a.values());
    }

    ImageBuffer::shutdownLibrary();
    std::puts("colormixer_test: OK");
    return 0;
}
