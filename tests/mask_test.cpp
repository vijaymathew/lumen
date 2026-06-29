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

    // Exclusive zone: a rectangle restricts a full (None) mask to its interior.
    {
        MaskSpec s;
        s.type = MaskSpec::None; // full coverage, gated only by the zone
        s.zoneFeather = 0.0f;
        MaskZoneShape rect;
        rect.kind = MaskZoneShape::Rect;
        rect.center = {0.5, 0.5};
        rect.half = {0.25, 0.25}; // central quarter
        s.zones.push_back(rect);
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, w / 2, h / 2) > 0.9f); // inside the rect
        CHECK(at(m, 2, 2) < 0.1f);         // outside → excluded
    }

    // Zone is the union of additive shapes minus subtractive ones.
    {
        MaskSpec s;
        s.type = MaskSpec::None;
        s.zoneFeather = 0.0f;
        MaskZoneShape add; // big ellipse covering most of the frame
        add.kind = MaskZoneShape::Ellipse;
        add.center = {0.5, 0.5};
        add.half = {0.45, 0.45};
        MaskZoneShape hole; // small subtractive ellipse at the centre
        hole.kind = MaskZoneShape::Ellipse;
        hole.subtract = true;
        hole.center = {0.5, 0.5};
        hole.half = {0.12, 0.12};
        s.zones.push_back(add);
        s.zones.push_back(hole);
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, w / 2, h / 2) < 0.1f);             // centre cut out
        CHECK(at(m, w / 2, h / 2 + h / 4) > 0.9f);     // included ring still selected
    }

    // A zone gates a non-trivial mask too: a gradient stays excluded outside.
    {
        MaskSpec s;
        s.type = MaskSpec::LinearGradient;
        s.gradFrom = {0.0, 0.5};
        s.gradTo = {1.0, 0.5};
        s.zoneFeather = 0.0f;
        MaskZoneShape rect;
        rect.center = {0.5, 0.5};
        rect.half = {0.2, 0.2};
        s.zones.push_back(rect);
        MaskBuffer m = evaluateMask(s, w, h);
        CHECK(at(m, w - 2, h / 2) < 0.1f); // gradient ~1 here, but outside the zone
        CHECK(at(m, w / 2, h / 2) > 0.4f); // inside the zone the gradient shows (~0.5)
    }

    // JSON round-trip (now including zones).
    {
        MaskSpec s;
        s.type = MaskSpec::Radial;
        s.center = {0.4, 0.6};
        s.radiusX = 0.25f;
        s.radiusY = 0.35f;
        s.angle = 0.5f;
        s.invert = true;
        s.zoneFeather = 0.08f;
        MaskZoneShape rect;
        rect.kind = MaskZoneShape::Rect;
        rect.center = {0.3, 0.7};
        rect.half = {0.15, 0.2};
        rect.angle = 0.2f;
        MaskZoneShape poly;
        poly.kind = MaskZoneShape::Polygon;
        poly.subtract = true;
        poly.points = {{0.1, 0.1}, {0.4, 0.1}, {0.25, 0.4}};
        s.zones.push_back(rect);
        s.zones.push_back(poly);
        MaskSpec r = MaskSpec::fromJson(s.toJson());
        CHECK(r == s);
    }

    std::puts("mask_test: OK");
    return 0;
}
