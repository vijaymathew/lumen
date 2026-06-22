#include "core/SelectiveMask.h"

#include "core/GuidedFilter.h"

#include <algorithm>
#include <cmath>

namespace {
float smoothstep(float e0, float e1, float x)
{
    if (e0 == e1)
        return x < e0 ? 0.0f : 1.0f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

MaskBuffer colorAffinityMask(const uint8_t *rgba, int width, int height, int bands,
                             float tr, float tg, float tb, float range)
{
    MaskBuffer mask;
    mask.width = width;
    mask.height = height;
    if (width <= 0 || height <= 0 || !rgba)
        return mask;

    const size_t n = static_cast<size_t>(width) * height;
    std::vector<float> raw(n);
    std::vector<float> guide(n);
    const float threshold = std::max(range, 0.02f);

    for (size_t i = 0; i < n; ++i) {
        const uint8_t *p = rgba + i * bands;
        const float r = p[0] / 255.0f;
        const float g = p[1] / 255.0f;
        const float b = p[2] / 255.0f;
        const float dr = r - tr, dg = g - tg, db = b - tb;
        const float d = std::sqrt(dr * dr + dg * dg + db * db);
        raw[i] = 1.0f - smoothstep(0.0f, threshold, d);
        guide[i] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    const int radius = std::max(2, std::min(width, height) / 64);
    mask.data = guidedFilter(guide, raw, width, height, radius, 0.02f);
    return mask;
}
