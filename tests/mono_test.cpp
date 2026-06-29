// Unit test for MonoNode: passthrough when off, weighted B&W conversion, the
// channel mixer, toning, and the preview-state contribution.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/MonoNode.h"

#include <QColor>

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

static bool near8(int a, int b) { return std::abs(a - b) <= 2; }

static Image solid(int r, int g, int b)
{
    const int w = 8, h = 8;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = static_cast<uint8_t>(r);
        px[i * 4 + 1] = static_cast<uint8_t>(g);
        px[i * 4 + 2] = static_cast<uint8_t>(b);
        px[i * 4 + 3] = 255;
    }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    Image red = solid(255, 0, 0);
    CHECK(!red.isNull());

    MonoNode node;

    // Disabled → passthrough (still red).
    QColor c = node.apply(red).toQImage().pixelColor(0, 0);
    CHECK(c.red() == 255 && c.green() == 0 && c.blue() == 0);

    // Enabled with default luma weights → grey ≈ 0.2126 * 255 ≈ 54, equal RGB.
    MonoValues v;
    v.enabled = true;
    node.setValues(v);
    c = node.apply(red).toQImage().pixelColor(0, 0);
    CHECK(near8(c.red(), 54) && near8(c.green(), 54) && near8(c.blue(), 54));
    CHECK(c.red() == c.green() && c.green() == c.blue()); // neutral grey

    // Red-only mixer → grey takes all of the red channel → white.
    v.mixR = 1.0f;
    v.mixG = 0.0f;
    v.mixB = 0.0f;
    node.setValues(v);
    c = node.apply(red).toQImage().pixelColor(0, 0);
    CHECK(near8(c.red(), 255) && near8(c.green(), 255) && near8(c.blue(), 255));

    // Toning with a warm hue tints the grey: red channel ends up warmer (higher)
    // than the blue channel. Use a mid-grey input so there's headroom both ways.
    Image grey = solid(128, 128, 128);
    MonoValues t;
    t.enabled = true;            // default luma weights → grey 128
    t.toneStrength = 1.0f;
    t.toneHue = 32.0f;           // warm / sepia
    node.setValues(t);
    c = node.apply(grey).toQImage().pixelColor(0, 0);
    CHECK(c.red() > c.blue());   // warm tint
    CHECK(near8(static_cast<int>(0.2126 * c.red() + 0.7152 * c.green()
                                 + 0.0722 * c.blue()), 128)); // luma preserved

    // Preview-state contribution: enabled sets monoEnabled + normalised weights.
    {
        MonoNode pv;
        PreviewState ps;
        pv.contributeToPreview(ps);
        CHECK(ps.monoEnabled == 0.0f); // off by default
        MonoValues e;
        e.enabled = true;
        e.mixR = 1.0f;
        e.mixG = 1.0f;
        e.mixB = 2.0f; // sum 4 → normalised 0.25/0.25/0.5
        pv.setValues(e);
        ps = PreviewState{};
        pv.contributeToPreview(ps);
        CHECK(ps.monoEnabled == 1.0f);
        CHECK(near8(static_cast<int>(ps.monoR * 100), 25));
        CHECK(near8(static_cast<int>(ps.monoB * 100), 50));
    }

    // Per-color mixer: a +Red band brightens a saturated red patch's grey, while
    // a neutral grey patch is unaffected (chroma = 0, so no band shifts it).
    {
        MonoNode m;
        MonoValues base;
        base.enabled = true; // neutral bands → plain luma grey
        m.setValues(base);
        const int neutralRed = m.apply(red).toQImage().pixelColor(0, 0).red();

        MonoValues boosted = base;
        boosted.band[0] = 1.0f; // Red band up
        m.setValues(boosted);
        const int boostedRed = m.apply(red).toQImage().pixelColor(0, 0).red();
        CHECK(boostedRed > neutralRed + 10); // red renders much brighter

        const int g0 = m.apply(grey).toQImage().pixelColor(0, 0).red();
        CHECK(near8(g0, 128)); // neutral grey untouched by the band
    }

    // The 8 bands round-trip through save/restore.
    {
        MonoNode a;
        MonoValues bv;
        bv.enabled = true;
        for (int i = 0; i < 8; ++i)
            bv.band[i] = (i - 3) * 0.1f;
        a.setValues(bv);
        MonoNode b;
        b.restoreState(a.saveState());
        CHECK(b.values() == a.values());
    }

    ImageBuffer::shutdownLibrary();
    std::puts("mono_test: OK");
    return 0;
}
