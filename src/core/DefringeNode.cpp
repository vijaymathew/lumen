// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/DefringeNode.h"

#include <algorithm>
#include <cmath>
#include <vector>

DefringeNode::DefringeNode()
    : EditNode(QStringLiteral("defringe"))
{
}

void DefringeNode::setValues(const Values &values)
{
    Values v = values;
    v.purple = std::clamp(v.purple, 0.0f, 100.0f);
    v.green = std::clamp(v.green, 0.0f, 100.0f);
    v.threshold = std::clamp(v.threshold, 0.0f, 100.0f);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

namespace {
float smoothstep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

Image DefringeNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled)
        return input;
    if (m_values.purple <= 0.0f && m_values.green <= 0.0f)
        return input;
    const int bands = vips_image_get_bands(input.handle());
    if (bands < 3)
        return input;

    // Separate alpha / extra bands; the colour conversion only wants 3 bands.
    VipsImage *rgb = nullptr, *alpha = nullptr;
    if (bands >= 4) {
        if (vips_extract_band(input.handle(), &rgb, 0, "n", 3, nullptr))
            return input;
        if (vips_extract_band(input.handle(), &alpha, 3, "n", bands - 3, nullptr)) {
            g_object_unref(rgb);
            return input;
        }
    } else {
        rgb = input.handle();
        g_object_ref(rgb);
    }

    const auto bail = [&](VipsImage *a) -> Image {
        if (a) g_object_unref(a);
        if (alpha) g_object_unref(alpha);
        return input;
    };

    // sRGB float -> L*a*b* (re-tag sRGB: extract dropped the interpretation).
    VipsImage *tagged = nullptr;
    if (vips_copy(rgb, &tagged, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr))
        return bail(rgb);
    g_object_unref(rgb);
    VipsImage *lab = nullptr;
    if (vips_colourspace(tagged, &lab, VIPS_INTERPRETATION_LAB, nullptr))
        return bail(tagged);
    g_object_unref(tagged);
    const int w = lab->Xsize, h = lab->Ysize;

    void *buf = vips_image_write_to_memory(lab, nullptr);
    g_object_unref(lab);
    if (!buf)
        return bail(nullptr);
    auto *px = static_cast<float *>(buf); // interleaved L,a,b per pixel

    // Parameters.
    const float pAmt = m_values.purple / 100.0f;
    const float gAmt = m_values.green / 100.0f;
    const float t0 = m_values.threshold / 100.0f * 60.0f; // L-gradient cutoff (Lab units)
    const float t1 = t0 + 20.0f;
    // Reference chroma directions (unit), in (a,b): purple ≈ +a,-b (magenta-violet),
    // green ≈ -a.
    constexpr float pa = 0.70710678f, pb = -0.70710678f;
    constexpr float ga = -1.0f, gb = 0.0f;

    const auto Lat = [&](int x, int y) -> float {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[(static_cast<size_t>(y) * w + x) * 3 + 0];
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float *p = px + (static_cast<size_t>(y) * w + x) * 3;
            const float a = p[1], b = p[2];
            const float chroma = std::sqrt(a * a + b * b);
            if (chroma < 1e-3f)
                continue;

            // Edge weight from the L gradient (central differences, clamped border).
            const float gx = std::abs(Lat(x + 1, y) - Lat(x - 1, y));
            const float gy = std::abs(Lat(x, y + 1) - Lat(x, y - 1));
            const float e = smoothstep(t0, t1, gx + gy);
            if (e <= 0.0f)
                continue;

            // Fringe-band membership: alignment of the chroma direction with the
            // purple/green references (squared to tighten the band), gated by how
            // saturated the pixel is (neutral pixels aren't fringe).
            const float ua = a / chroma, ub = b / chroma;
            float pAlign = std::max(0.0f, ua * pa + ub * pb);
            float gAlign = std::max(0.0f, ua * ga + ub * gb);
            pAlign *= pAlign;
            gAlign *= gAlign;
            const float satW = smoothstep(6.0f, 20.0f, chroma);

            const float reduce =
                std::clamp(e * (pAmt * pAlign + gAmt * gAlign) * satW, 0.0f, 1.0f);
            p[1] = a * (1.0f - reduce);
            p[2] = b * (1.0f - reduce);
        }
    }

    VipsImage *labNew = vips_image_new_from_memory_copy(
        buf, static_cast<size_t>(w) * h * 3 * sizeof(float), w, h, 3, VIPS_FORMAT_FLOAT);
    g_free(buf);
    if (!labNew)
        return bail(nullptr);

    VipsImage *labTag = nullptr;
    if (vips_copy(labNew, &labTag, "interpretation", VIPS_INTERPRETATION_LAB, nullptr))
        return bail(labNew);
    g_object_unref(labNew);

    VipsImage *srgb = nullptr;
    if (vips_colourspace(labTag, &srgb, VIPS_INTERPRETATION_sRGB, nullptr))
        return bail(labTag);
    g_object_unref(labTag);

    VipsImage *asFloat = nullptr;
    if (vips_cast(srgb, &asFloat, VIPS_FORMAT_FLOAT, nullptr))
        return bail(srgb);
    g_object_unref(srgb);
    VipsImage *result = nullptr;
    if (vips_copy(asFloat, &result, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr))
        return bail(asFloat);
    g_object_unref(asFloat);

    // Re-attach the carried-through alpha.
    if (alpha) {
        VipsImage *withAlpha[2] = {result, alpha};
        VipsImage *full = nullptr;
        const int rc = vips_bandjoin(withAlpha, &full, 2, nullptr);
        g_object_unref(result);
        g_object_unref(alpha);
        if (rc)
            return input;
        result = full;
    }
    return Image::adopt(result);
}

QJsonObject DefringeNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("defringeEnabled")] = m_values.enabled;
    state[QStringLiteral("purple")] = m_values.purple;
    state[QStringLiteral("green")] = m_values.green;
    state[QStringLiteral("threshold")] = m_values.threshold;
    return state;
}

void DefringeNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    Values v;
    v.enabled = state.value(QStringLiteral("defringeEnabled")).toBool(false);
    v.purple = static_cast<float>(state.value(QStringLiteral("purple")).toDouble(50.0));
    v.green = static_cast<float>(state.value(QStringLiteral("green")).toDouble(50.0));
    v.threshold = static_cast<float>(state.value(QStringLiteral("threshold")).toDouble(25.0));
    setValues(v);
}
