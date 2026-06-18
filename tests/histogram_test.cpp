// Unit test for computeHistogram: a null image is invalid; solid colours land
// in the expected per-channel bin.

#include "core/Histogram.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"

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

static Image solid(int r, int g, int b)
{
    const int w = 64, h = 64;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        px[i * 4 + 0] = static_cast<uint8_t>(r);
        px[i * 4 + 1] = static_cast<uint8_t>(g);
        px[i * 4 + 2] = static_cast<uint8_t>(b);
        px[i * 4 + 3] = 255;
    }
    return Image::fromInterleaved(px.data(), w, h, 4);
}

// Index of the largest bin for a channel.
static int argmax(const std::array<uint32_t, 256> &bin)
{
    int best = 0;
    for (int i = 1; i < 256; ++i)
        if (bin[i] > bin[best])
            best = i;
    return best;
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Null image → invalid.
    CHECK(!computeHistogram(Image()).valid);

    // Solid red: red bunches at 255, green/blue at 0.
    {
        HistogramData h = computeHistogram(solid(255, 0, 0));
        CHECK(h.valid && h.peak > 0);
        CHECK(argmax(h.bins[0]) >= 253);
        CHECK(argmax(h.bins[1]) <= 2);
        CHECK(argmax(h.bins[2]) <= 2);
    }

    // Mid grey: every channel peaks near 128.
    {
        HistogramData h = computeHistogram(solid(128, 128, 128));
        CHECK(h.valid);
        for (int c = 0; c < 3; ++c) {
            const int m = argmax(h.bins[c]);
            CHECK(m >= 126 && m <= 130);
        }
    }

    ImageBuffer::shutdownLibrary();
    std::puts("histogram_test: OK");
    return 0;
}
