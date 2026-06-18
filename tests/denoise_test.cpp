// Unit test for DenoiseNode: passthrough when off, high-frequency luma noise is
// reduced, a hard edge is preserved (edge-aware guided filter), and parameters
// round-trip through save/restore.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/DenoiseNode.h"

#include <QImage>

#include <cmath>
#include <cstdint>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// Grey checkerboard: mid ± amp on alternating pixels — pure high-freq luma noise.
static Image checker(int mid, int amp, int w = 32, int h = 32)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            const int v = mid + (((x + y) & 1) ? amp : -amp);
            px[i + 0] = px[i + 1] = px[i + 2] = static_cast<uint8_t>(v);
            px[i + 3] = 255;
        }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

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

// Mean absolute deviation of the red channel from `centre`.
static double meanDev(const QImage &q, int centre)
{
    double sum = 0;
    long n = 0;
    for (int y = 0; y < q.height(); ++y)
        for (int x = 0; x < q.width(); ++x) {
            sum += std::abs(q.pixelColor(x, y).red() - centre);
            ++n;
        }
    return n ? sum / n : 0.0;
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Disabled → passthrough.
    {
        DenoiseNode node;
        Image in = checker(128, 25);
        CHECK(node.apply(in).toQImage().pixelColor(0, 0).red()
              == in.toQImage().pixelColor(0, 0).red());
    }

    DenoiseNode node;
    DenoiseNode::Values v;
    v.enabled = true;
    v.luma = 100.0f;
    v.chroma = 100.0f;
    node.setValues(v);

    // High-frequency luma noise is smoothed: deviation from the mid grey drops.
    {
        Image in = checker(128, 25);
        const double before = meanDev(in.toQImage(), 128);
        const double after = meanDev(node.apply(in).toQImage(), 128);
        CHECK(before > 20.0);          // sanity: the input really is noisy
        CHECK(after < before * 0.85);  // meaningfully smoother
    }

    // A hard edge is preserved (not blurred into a ramp).
    {
        Image out = node.apply(edge(80, 180));
        CHECK(!out.isNull());
        QImage q = out.toQImage();
        const int y = q.height() / 2;
        const int left = q.pixelColor(2, y).red();
        const int right = q.pixelColor(q.width() - 3, y).red();
        CHECK(right - left > 80); // most of the 100-level step survives
    }

    // Serialise round-trip.
    {
        DenoiseNode a;
        DenoiseNode::Values av;
        av.enabled = true;
        av.luma = 40.0f;
        av.chroma = 65.0f;
        a.setValues(av);
        DenoiseNode b;
        b.restoreState(a.saveState());
        CHECK(b.values() == av);
    }

    ImageBuffer::shutdownLibrary();
    std::puts("denoise_test: OK");
    return 0;
}
