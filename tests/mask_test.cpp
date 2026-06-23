// Unit test for the layer mask abstraction (MaskSpec + evaluateMask). Pure CPU.

#include "core/MaskSpec.h"

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
    const int w = 64, h = 64;
    const auto at = [w](const MaskBuffer &m, int x, int y) {
        return m.data[static_cast<size_t>(y) * w + x];
    };

    // None → fully selected everywhere.
    {
        MaskSpec s;
        s.type = MaskSpec::None;
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, 0, 0) > 0.99f && at(m, w - 1, h - 1) > 0.99f);
    }

    // Linear gradient left→right: ~0 at left, ~1 at right, ~0.5 in the middle.
    {
        MaskSpec s;
        s.type = MaskSpec::LinearGradient;
        s.gradFrom = {0.0, 0.5};
        s.gradTo = {1.0, 0.5};
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, 1, h / 2) < 0.1f);
        CHECK(at(m, w - 2, h / 2) > 0.9f);
        CHECK(std::abs(at(m, w / 2, h / 2) - 0.5f) < 0.05f);
        // Invert flips it.
        s.invert = true;
        MaskBuffer mi = evaluateMask(s, w, h);
        CHECK(at(mi, 1, h / 2) > 0.9f && at(mi, w - 2, h / 2) < 0.1f);
    }

    // Radial (circle) at the centre: selected inside, not at the corners.
    {
        MaskSpec s;
        s.type = MaskSpec::Radial;
        s.center = {0.5, 0.5};
        s.radiusX = s.radiusY = 0.3f;
        s.feather = 0.1f;
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, w / 2, h / 2) > 0.9f); // centre selected
        CHECK(at(m, 0, 0) < 0.1f);         // corner not
        // Outside variant inverts the relationship.
        s.radialInside = false;
        MaskBuffer mo = evaluateMask(s, w, h);
        CHECK(at(mo, w / 2, h / 2) < 0.1f);
        CHECK(at(mo, 0, 0) > 0.9f);
    }

    // Elliptical: wide in x, narrow in y.
    {
        MaskSpec s;
        s.type = MaskSpec::Radial;
        s.center = {0.5, 0.5};
        s.radiusX = 0.45f;
        s.radiusY = 0.1f;
        s.feather = 0.05f;
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, w / 8, h / 2) > 0.9f);     // far along the wide axis: inside
        CHECK(at(m, w / 2, h / 8) < 0.1f);     // a little along the narrow axis: outside
    }

    // Luminosity range needs pixel data: mid-grey selected by a midtone band.
    {
        std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4, 0);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            img[i * 4 + 0] = img[i * 4 + 1] = img[i * 4 + 2] = 128; // grey
            img[i * 4 + 3] = 255;
        }
        MaskSpec s;
        s.type = MaskSpec::Luminosity;
        s.low = 0.3f;
        s.high = 0.7f;
        s.feather = 0.05f;
        MaskBuffer m = evaluateMask(s, w, h, img.data(), 4);
        CHECK(at(m, w / 2, h / 2) > 0.9f); // 0.5 luma is inside [0.3,0.7]
        s.low = 0.0f;
        s.high = 0.4f; // shadows only → excludes 0.5
        MaskBuffer m2 = evaluateMask(s, w, h, img.data(), 4);
        CHECK(at(m2, w / 2, h / 2) < 0.1f);
    }

    // JSON round-trip.
    {
        MaskSpec s;
        s.type = MaskSpec::Radial;
        s.center = {0.4, 0.6};
        s.radiusX = 0.25f;
        s.radiusY = 0.35f;
        s.angle = 0.5f;
        s.invert = true;
        MaskSpec r = MaskSpec::fromJson(s.toJson());
        CHECK(r == s);
    }

    std::puts("mask_test: OK");
    return 0;
}
