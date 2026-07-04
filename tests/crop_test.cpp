// Unit test for CropState + applyCrop: identity passthrough, crop-rect and
// rotation/flip output dimensions, JSON round-trip, and a corner-pixel check
// that a 90° rotation moves pixels to the expected place. These guarantee the
// export path (EditGraph::result → applyCrop) matches the documented model;
// the GPU present pass mirrors the same transform for preview == export.

#include "core/CropState.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"

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

// Builds a w×h image with one red marker pixel at (mx,my); everything else black.
static Image marker(int w, int h, int mx, int my)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
        px[i * 4 + 3] = 255; // opaque
    const size_t idx = (static_cast<size_t>(my) * w + mx) * 4;
    px[idx + 0] = 255;
    px[idx + 1] = 0;
    px[idx + 2] = 0;
    return Image::fromInterleaved(px.data(), w, h, 4);
}

// Builds a solid opaque red w×h image (no markers); used to probe geometry where
// interpolation would blur a single-pixel marker.
static Image solidRed(int w, int h)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
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

    const int W = 40, H = 20;
    Image src = marker(W, H, 0, 0); // red at top-left

    // 1. Identity = passthrough (same dimensions, marker untouched).
    {
        CropState c; // default identity
        CHECK(c.isIdentity());
        Image out = applyCrop(src, c);
        CHECK(out.width() == W && out.height() == H);
        QImage q = out.toQImage();
        CHECK(qRed(q.pixel(0, 0)) > 200);
    }

    // 2. Crop rect → scaled output dimensions (half width, half height).
    {
        CropState c;
        c.rect = QRectF(0.0, 0.0, 0.5, 0.5);
        CHECK(!c.isIdentity());
        Image out = applyCrop(src, c);
        CHECK(out.width() == W / 2 && out.height() == H / 2);
    }

    // 3. Rotation 90/270 swaps width and height; 180 keeps them.
    {
        CropState c90;
        c90.rotation = 90;
        Image r90 = applyCrop(src, c90);
        CHECK(r90.width() == H && r90.height() == W);

        CropState c270;
        c270.rotation = 270;
        Image r270 = applyCrop(src, c270);
        CHECK(r270.width() == H && r270.height() == W);

        CropState c180;
        c180.rotation = 180;
        Image r180 = applyCrop(src, c180);
        CHECK(r180.width() == W && r180.height() == H);
    }

    // 4. Flips keep dimensions but move the marker to the opposite corner.
    {
        CropState fh;
        fh.flipH = true;
        Image out = applyCrop(src, fh);
        CHECK(out.width() == W && out.height() == H);
        QImage q = out.toQImage();
        CHECK(qRed(q.pixel(W - 1, 0)) > 200); // top-left → top-right

        CropState fv;
        fv.flipV = true;
        QImage qv = applyCrop(src, fv).toQImage();
        CHECK(qRed(qv.pixel(0, H - 1)) > 200); // top-left → bottom-left
    }

    // 5. 90° clockwise rotation: the top-left marker lands at the new top-right.
    {
        CropState c;
        c.rotation = 90;
        Image out = applyCrop(src, c);
        CHECK(out.width() == H && out.height() == W);
        QImage q = out.toQImage();
        CHECK(qRed(q.pixel(out.width() - 1, 0)) > 200);
    }

    // 6. JSON round-trip preserves every field.
    {
        CropState c;
        c.rotation = 270;
        c.flipH = true;
        c.flipV = false;
        c.straighten = -3.5;
        c.rect = QRectF(0.1, 0.2, 0.5, 0.6);
        CropState back = CropState::fromJson(c.toJson());
        CHECK(back.rotation == c.rotation);
        CHECK(back.flipH == c.flipH && back.flipV == c.flipV);
        CHECK(qAbs(back.straighten - c.straighten) < 1e-9);
        CHECK(qAbs(back.rect.x() - c.rect.x()) < 1e-9);
        CHECK(qAbs(back.rect.width() - c.rect.width()) < 1e-9);
        CHECK(back == c);
    }

    // 7. The `enabled` flag: a disabled crop renders as identity but keeps its
    //    geometry (so it still reports non-identity and round-trips).
    {
        CropState c;
        c.rect = QRectF(0.0, 0.0, 0.5, 0.5);
        c.enabled = false;
        CHECK(!c.isIdentity());            // geometry is still present
        Image out = applyCrop(src, c);     // but disabled → passthrough
        CHECK(out.width() == W && out.height() == H);

        CropState back = CropState::fromJson(c.toJson());
        CHECK(back.enabled == false && back == c);

        // Back-compat: a project saved before the flag existed defaults to enabled.
        QJsonObject legacy = c.toJson();
        legacy.remove(QStringLiteral("enabled"));
        CHECK(CropState::fromJson(legacy).enabled == true);
    }

    // 8. Straighten: non-zero angle is non-identity, clamps to [-45, 45], and
    //    defaults back to 0 for projects saved before the field existed.
    {
        CropState c;
        c.straighten = 5.0;
        CHECK(!c.isIdentity());        // a pure tilt is still an adjustment
        c.straighten = 0.0;
        CHECK(c.isIdentity());

        CropState over;
        over.straighten = 90.0;
        over.sanitize();
        CHECK(qAbs(over.straighten - 45.0) < 1e-9); // clamped
        over.straighten = -90.0;
        over.sanitize();
        CHECK(qAbs(over.straighten + 45.0) < 1e-9);

        CropState tilted;
        tilted.straighten = 12.0;
        QJsonObject legacy = tilted.toJson();
        legacy.remove(QStringLiteral("straighten"));
        CHECK(qAbs(CropState::fromJson(legacy).straighten) < 1e-9);
    }

    // 9. Straighten geometry: a full-frame tilt keeps the oriented dimensions
    //    (content is inset into a centred rect of the enlarged rotated canvas),
    //    the frame centre stays covered, and the corners rotate away to the
    //    transparent background.
    {
        Image red = solidRed(W, H);
        CropState c;
        c.straighten = 20.0; // full-frame rect (0,0,1,1)
        Image out = applyCrop(red, c);
        CHECK(out.width() == W && out.height() == H); // rect normalized to oriented frame
        QImage q = out.toQImage();
        CHECK(qAlpha(q.pixel(W / 2, H / 2)) > 200); // centre still covered by content
        CHECK(qRed(q.pixel(W / 2, H / 2)) > 200);
        CHECK(qAlpha(q.pixel(0, 0)) < 50);          // a corner rotated to background
    }

    std::printf("crop_test OK\n");
    return 0;
}
