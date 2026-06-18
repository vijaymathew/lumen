// Unit test for VignetteParams + applyVignette: identity passthrough, that a
// negative amount darkens corners more than the centre, midpoint behaviour, and
// JSON round-trip. The gain math here is mirrored in present.frag (the
// preview==export contract — see docs/VIGNETTE.md).

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/Vignette.h"

#include <QImage>
#include <QJsonObject>

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

// A flat mid-grey image (so any darkening comes purely from the vignette).
static Image grey(int w, int h)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = 128;
        px[i * 4 + 1] = 128;
        px[i * 4 + 2] = 128;
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

    const int W = 80, H = 60;
    Image src = grey(W, H);

    // 1. Identity passthrough (disabled, or amount 0) → unchanged dimensions/centre.
    {
        VignetteParams v; // disabled
        CHECK(v.isIdentity());
        Image out = applyVignette(src, v);
        CHECK(out.width() == W && out.height() == H);
        QImage q = out.toQImage();
        CHECK(qRed(q.pixel(W / 2, H / 2)) == 128);

        VignetteParams z;
        z.enabled = true;
        z.amount = 0.0f;
        CHECK(z.isIdentity());
    }

    // 2. Negative amount darkens the corner strictly more than the centre.
    {
        VignetteParams v;
        v.enabled = true;
        v.amount = -80.0f;
        v.midpoint = 30.0f;
        v.feather = 60.0f;
        Image out = applyVignette(src, v);
        QImage q = out.toQImage();
        const int centre = qRed(q.pixel(W / 2, H / 2));
        const int corner = qRed(q.pixel(1, 1));
        CHECK(corner < centre);   // corners pulled down
        CHECK(centre >= 120);     // centre near-untouched at midpoint 30
    }

    // 3. A larger midpoint keeps more of the centre bright (corner still darkens).
    {
        VignetteParams a;
        a.enabled = true; a.amount = -80.0f; a.midpoint = 10.0f; a.feather = 40.0f;
        VignetteParams b = a; b.midpoint = 70.0f;
        QImage qa = applyVignette(src, a).toQImage();
        QImage qb = applyVignette(src, b).toQImage();
        // At a quarter in from the corner, the wider midpoint stays brighter.
        const int xa = qRed(qa.pixel(W / 4, H / 4));
        const int xb = qRed(qb.pixel(W / 4, H / 4));
        CHECK(xb >= xa);
    }

    // 4. JSON round-trip preserves every field.
    {
        VignetteParams v;
        v.enabled = true;
        v.amount = -42.0f;
        v.midpoint = 33.0f;
        v.roundness = 25.0f;
        v.feather = 66.0f;
        VignetteParams back = VignetteParams::fromJson(v.toJson());
        CHECK(back == v);
    }

    std::printf("vignette_test OK\n");
    return 0;
}
