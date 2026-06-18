// Unit test for the self-contained guided filter and the colour-affinity mask.
// Pure CPU math — no libvips/Qt runtime needed.

#include "core/GuidedFilter.h"
#include "core/SelectiveMask.h"

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

int main()
{
    const int w = 32, h = 32;

    // Constant src -> constant output (a flat region is preserved exactly).
    {
        std::vector<float> guide(w * h, 0.3f);
        std::vector<float> src(w * h, 0.5f);
        std::vector<float> q = guidedFilter(guide, src, w, h, 4, 0.01f);
        for (float v : q)
            CHECK(std::abs(v - 0.5f) < 0.02f);
    }

    // Colour-affinity mask: left half red, right half blue; target = red.
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t *p = img.data() + (static_cast<size_t>(y) * w + x) * 4;
            const bool left = x < w / 2;
            p[0] = left ? 200 : 20;  // R
            p[1] = 40;               // G
            p[2] = left ? 40 : 200;  // B
            p[3] = 255;
        }
    }
    MaskBuffer m = colorAffinityMask(img.data(), w, h, 4,
                                     200 / 255.0f, 40 / 255.0f, 40 / 255.0f, 0.3f);
    CHECK(m.width == w && m.height == h);
    CHECK(!m.isEmpty());

    const float left = m.data[(h / 2) * w + w / 4];   // inside the red region
    const float right = m.data[(h / 2) * w + 3 * w / 4]; // inside the blue region
    CHECK(left > 0.7f);  // matches the target colour
    CHECK(right < 0.3f); // far from the target colour

    // Brush stamp: Add paints toward 1 near the centre, leaves corners untouched.
    MaskBuffer bm;
    bm.width = 32;
    bm.height = 32;
    bm.data.assign(32 * 32, 0.0f);
    stampBrush(bm, 16, 16, 8, 0.5f, true);
    CHECK(bm.data[16 * 32 + 16] > 0.9f);
    CHECK(bm.data[0] < 0.1f);

    // Subtract from a full mask carves a hole at the centre.
    MaskBuffer full;
    full.width = 32;
    full.height = 32;
    full.data.assign(32 * 32, 1.0f);
    stampBrush(full, 16, 16, 8, 0.5f, false);
    CHECK(full.data[16 * 32 + 16] < 0.1f);
    CHECK(full.data[0] > 0.9f);

    // Upscale preserves dimensions and corner values.
    MaskBuffer small;
    small.width = 2;
    small.height = 2;
    small.data = {0.0f, 1.0f, 1.0f, 0.0f};
    MaskBuffer up = upscaleMask(small, 8, 8);
    CHECK(up.width == 8 && up.height == 8);
    CHECK(up.data[0] < 0.2f);             // top-left ~ 0
    CHECK(up.data[7] > 0.8f);             // top-right ~ 1

    std::puts("guided_filter_test: OK");
    return 0;
}
