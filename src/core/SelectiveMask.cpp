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

void stampBrush(MaskBuffer &mask, float cx, float cy, float radius, float hardness,
                bool add)
{
    if (mask.isEmpty() || radius <= 0.0f)
        return;
    const int w = mask.width;
    const int h = mask.height;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + radius)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + radius)));
    const float inner = radius * std::clamp(hardness, 0.0f, 0.99f);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = x - cx, dy = y - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > radius)
                continue;
            const float weight = 1.0f - smoothstep(inner, radius, d);
            float &m = mask.data[static_cast<size_t>(y) * w + x];
            m = add ? std::max(m, weight) : std::min(m, 1.0f - weight);
        }
    }
}

MaskBuffer upscaleMask(const MaskBuffer &src, int width, int height)
{
    MaskBuffer out;
    out.width = width;
    out.height = height;
    out.data.assign(static_cast<size_t>(width) * height, 0.0f);
    if (src.isEmpty() || width <= 0 || height <= 0)
        return out;

    const int sw = src.width, sh = src.height;
    for (int y = 0; y < height; ++y) {
        const float sy = std::clamp((y + 0.5f) * sh / height - 0.5f, 0.0f, sh - 1.0f);
        const int y0 = static_cast<int>(std::floor(sy));
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = sy - y0;
        for (int x = 0; x < width; ++x) {
            const float sx = std::clamp((x + 0.5f) * sw / width - 0.5f, 0.0f, sw - 1.0f);
            const int x0 = static_cast<int>(std::floor(sx));
            const int x1 = std::min(x0 + 1, sw - 1);
            const float fx = sx - x0;
            const float a = src.data[static_cast<size_t>(y0) * sw + x0];
            const float b = src.data[static_cast<size_t>(y0) * sw + x1];
            const float c = src.data[static_cast<size_t>(y1) * sw + x0];
            const float d = src.data[static_cast<size_t>(y1) * sw + x1];
            const float top = a + (b - a) * fx;
            const float bot = c + (d - c) * fx;
            out.data[static_cast<size_t>(y) * width + x] = top + (bot - top) * fy;
        }
    }
    return out;
}
