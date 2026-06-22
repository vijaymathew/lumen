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

    std::puts("guided_filter_test: OK");
    return 0;
}
