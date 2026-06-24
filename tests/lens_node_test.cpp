// Unit test for LensCorrectionNode: the perspective homography math (identity
// when neutral, centre is a fixed point, zoom samples inward), geometric
// passthrough when neutral, and the serialise round-trip. The Lensfun-backed
// automatic correction is only exercised for non-crashing behaviour, since the
// profile database may be absent in CI.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/LensCorrectionNode.h"

#include <QColor>

#include <array>
#include <cmath>
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

static bool neard(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// Map a destination pixel through a row-major 3x3 backward map.
static void mapPoint(const std::array<double, 9> &H, double x, double y, double &sx,
                     double &sy)
{
    const double u = H[0] * x + H[1] * y + H[2];
    const double v = H[3] * x + H[4] * y + H[5];
    const double w = H[6] * x + H[7] * y + H[8];
    sx = u / w;
    sy = v / w;
}

// 8x8 image with a horizontal gradient (value = x*32 in every colour channel).
static Image gradient()
{
    const int w = 8, h = 8;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            const uint8_t v = static_cast<uint8_t>(x * 32);
            px[i + 0] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    const int W = 8, H = 8;
    const double cx = W * 0.5, cy = H * 0.5;

    // --- Neutral perspective is the identity --------------------------------
    {
        auto M = LensCorrectionNode::perspectiveBackMap(W, H, 0, 0, 0, 1.0f);
        const std::array<double, 9> I = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        for (int i = 0; i < 9; ++i)
            CHECK(neard(M[i] / M[8], I[i], 1e-6)); // normalise scale
    }

    // --- The image centre is a fixed point of any rotation/keystone ----------
    for (auto p : {std::array<float, 3>{20, 0, 0}, std::array<float, 3>{0, -15, 0},
                   std::array<float, 3>{0, 0, 30}}) {
        auto M = LensCorrectionNode::perspectiveBackMap(W, H, p[0], p[1], p[2], 1.0f);
        double sx, sy;
        mapPoint(M, cx, cy, sx, sy);
        CHECK(neard(sx, cx, 1e-6) && neard(sy, cy, 1e-6));
    }

    // --- Zoom (scale 2) samples inward: centre fixed, corner → quarter point --
    {
        auto M = LensCorrectionNode::perspectiveBackMap(W, H, 0, 0, 0, 2.0f);
        double sx, sy;
        mapPoint(M, cx, cy, sx, sy);
        CHECK(neard(sx, cx, 1e-6) && neard(sy, cy, 1e-6)); // centre fixed
        mapPoint(M, 0, 0, sx, sy);
        CHECK(neard(sx, cx * 0.5, 1e-6) && neard(sy, cy * 0.5, 1e-6));
    }

    // --- Geometric passthrough when nothing is active -----------------------
    {
        Image g = gradient();
        CHECK(!g.isNull());
        LensCorrectionNode node; // defaults: no lens match, neutral perspective
        Image out = node.apply(g);
        QColor a = g.toQImage().pixelColor(7, 0);
        QColor b = out.toQImage().pixelColor(7, 0);
        CHECK(a.red() == b.red()); // unchanged
    }

    // --- Zoom actually resamples: centre preserved, corner brightens ---------
    {
        Image g = gradient();
        LensCorrectionNode node;
        LensCorrectionNode::Params p = node.params();
        p.scale = 2.0f;
        node.setParams(p);
        Image out = node.apply(g);
        CHECK(!out.isNull());
        CHECK(out.width() == W && out.height() == H);
        QImage q = out.toQImage();
        // Corner (0,0) now samples near (2,2) → value ≈ 64, up from 0.
        CHECK(q.pixelColor(0, 0).red() > 40);
        // Centre stays put (gradient value at x=4 is 128).
        CHECK(std::abs(q.pixelColor(4, 4).red() - 128) <= 24);
    }

    // --- Serialise round-trip ------------------------------------------------
    {
        LensCorrectionNode a;
        LensCorrectionNode::Params p;
        p.cameraMaker = QStringLiteral("Canon");
        p.cameraModel = QStringLiteral("Canon EOS 5D Mark III");
        p.lensModel = QStringLiteral("Canon EF 24-70mm f/2.8L II USM");
        p.focalLength = 35.0f;
        p.aperture = 8.0f;
        p.focusDistance = 3.0f;
        p.cropFactor = 1.0f;
        p.distortion = true;
        p.distortionAmount = 0.5f;
        p.tca = false;
        p.vignetting = true;
        p.vignettingAmount = 0.75f;
        p.keystoneV = 10.0f;
        p.keystoneH = -5.0f;
        p.rotate = 2.0f;
        p.scale = 1.1f;
        a.setParams(p);

        LensCorrectionNode b;
        b.restoreState(a.saveState());
        CHECK(b.params() == p);
    }

    // lensMatched() must be callable without crashing whether or not Lensfun /
    // its database is present.
    {
        LensCorrectionNode node;
        LensCorrectionNode::Params p;
        p.lensModel = QStringLiteral("Definitely Not A Real Lens 999mm");
        node.setParams(p);
        (void)node.lensMatched();
    }

    ImageBuffer::shutdownLibrary();
    std::puts("lens_node_test: OK");
    return 0;
}
