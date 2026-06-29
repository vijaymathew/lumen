// Unit test for GrainNode: passthrough when off, adds zero-mean noise when on,
// deterministic (fixed seed), and round-trips through save/restore.

#include "core/GrainNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"

#include <QColor>
#include <QImage>

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

static Image solid(int v, int w = 64, int h = 64)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = static_cast<uint8_t>(v);
        px[i * 4 + 3] = 255;
    }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

static double meanLuma(const QImage &q)
{
    double sum = 0.0;
    for (int y = 0; y < q.height(); ++y)
        for (int x = 0; x < q.width(); ++x) {
            const QColor c = q.pixelColor(x, y);
            sum += 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
        }
    return sum / (q.width() * q.height());
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    const int w = 64, h = 64;
    Image grey = solid(128, w, h);
    CHECK(!grey.isNull());

    GrainNode node;

    // Disabled → exact passthrough.
    {
        QImage q = node.apply(grey).toQImage();
        CHECK(q.width() == w && q.height() == h);
        QColor c = q.pixelColor(0, 0);
        CHECK(c.red() == 128 && c.green() == 128 && c.blue() == 128);
    }

    // Enabled → output differs but keeps the size and the mean (noise is ~zero-mean).
    GrainNode::Values v;
    v.enabled = true;
    v.amount = 80.0f;
    v.size = 2.0f;
    node.setValues(v);
    QImage g1 = node.apply(grey).toQImage();
    CHECK(g1.width() == w && g1.height() == h);
    int differing = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (g1.pixelColor(x, y).red() != 128)
                ++differing;
    CHECK(differing > w * h / 4);                 // grain actually applied
    CHECK(std::abs(meanLuma(g1) - 128.0) < 4.0);  // mean preserved (zero-mean noise)

    // Monochrome grain: each pixel's R==G==B (same delta added to all channels).
    {
        const QColor c = g1.pixelColor(10, 10);
        CHECK(c.red() == c.green() && c.green() == c.blue());
    }

    // Deterministic: a second apply gives the identical result (fixed seed).
    {
        QImage g2 = node.apply(grey).toQImage();
        bool identical = true;
        for (int y = 0; y < h && identical; ++y)
            for (int x = 0; x < w; ++x)
                if (g1.pixelColor(x, y) != g2.pixelColor(x, y)) {
                    identical = false;
                    break;
                }
        CHECK(identical);
    }

    // Values round-trip through save/restore.
    {
        GrainNode a;
        GrainNode::Values sv;
        sv.enabled = true;
        sv.amount = 42.0f;
        sv.size = 3.5f;
        a.setValues(sv);
        GrainNode b;
        b.restoreState(a.saveState());
        CHECK(b.values() == a.values());
    }

    ImageBuffer::shutdownLibrary();
    std::puts("grain_test: OK");
    return 0;
}
