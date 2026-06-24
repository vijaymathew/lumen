// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/DenoiseNode.h"

#include "core/GuidedFilter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

DenoiseNode::DenoiseNode()
    : EditNode(QStringLiteral("denoise"))
{
}

void DenoiseNode::setValues(const Values &values)
{
    Values v = values;
    v.luma = std::clamp(v.luma, 0.0f, 100.0f);
    v.chroma = std::clamp(v.chroma, 0.0f, 100.0f);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

Image DenoiseNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled)
        return input;
    if (m_values.luma <= 0.0f && m_values.chroma <= 0.0f)
        return input;
    const int bands = vips_image_get_bands(input.handle());
    if (bands < 3)
        return input; // single-channel: nothing to do here

    // 1. Separate alpha (and any extra bands) — the colour conversion only wants
    //    the three colour bands; alpha is carried through verbatim.
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

    const auto bail = [&](VipsImage *a, VipsImage *b, VipsImage *c) -> Image {
        if (a) g_object_unref(a);
        if (b) g_object_unref(b);
        if (c) g_object_unref(c);
        if (alpha) g_object_unref(alpha);
        return input;
    };

    // 2. sRGB float -> L*a*b* (re-tag sRGB: extract dropped the interpretation).
    VipsImage *tagged = nullptr;
    if (vips_copy(rgb, &tagged, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr))
        return bail(rgb, nullptr, nullptr);
    g_object_unref(rgb);
    VipsImage *lab = nullptr;
    if (vips_colourspace(tagged, &lab, VIPS_INTERPRETATION_LAB, nullptr))
        return bail(tagged, nullptr, nullptr);
    g_object_unref(tagged);
    const int w = lab->Xsize, h = lab->Ysize;

    // 3. Chroma: blur the a/b channels (take them from a blurred copy). L is
    //    always taken from the unblurred image.
    VipsImage *chromaSrc = lab;
    g_object_ref(chromaSrc);
    if (m_values.chroma > 0.0f) {
        const double sigma = static_cast<double>(m_values.chroma) / 100.0 * 4.0;
        VipsImage *blurred = nullptr;
        if (!vips_gaussblur(lab, &blurred, sigma, nullptr)) {
            g_object_unref(chromaSrc);
            chromaSrc = blurred;
        }
    }
    VipsImage *L = nullptr, *A = nullptr, *B = nullptr;
    const bool split = !vips_extract_band(lab, &L, 0, nullptr)
        && !vips_extract_band(chromaSrc, &A, 1, nullptr)
        && !vips_extract_band(chromaSrc, &B, 2, nullptr);
    g_object_unref(chromaSrc);
    g_object_unref(lab);
    if (!split)
        return bail(L, A, B);

    // 4. Luma: edge-preserving guided filter on L (normalised to [0,1]).
    if (m_values.luma > 0.0f) {
        void *buf = vips_image_write_to_memory(L, nullptr);
        if (buf) {
            auto *lp = static_cast<float *>(buf);
            const long long n = static_cast<long long>(w) * h;
            std::vector<float> norm(static_cast<size_t>(n));
            for (long long i = 0; i < n; ++i)
                norm[i] = lp[i] / 100.0f;
            const int radius = 2 + static_cast<int>(std::lround(m_values.luma / 100.0 * 4.0));
            const float eps = std::pow(m_values.luma / 100.0f * 0.1f, 2.0f);
            const std::vector<float> smooth = guidedFilter(norm, norm, w, h, radius, eps);
            for (long long i = 0; i < n; ++i)
                lp[i] = std::clamp(smooth[i] * 100.0f, 0.0f, 100.0f);
            VipsImage *Lnew = vips_image_new_from_memory_copy(
                buf, static_cast<size_t>(n) * sizeof(float), w, h, 1, VIPS_FORMAT_FLOAT);
            g_free(buf);
            if (Lnew) {
                g_object_unref(L);
                L = Lnew;
            }
        }
    }

    // 5. Recombine L,a,b -> Lab -> sRGB, back to the float working format.
    VipsImage *labBands[3] = {L, A, B};
    VipsImage *joined = nullptr;
    const int rc = vips_bandjoin(labBands, &joined, 3, nullptr);
    g_object_unref(L);
    g_object_unref(A);
    g_object_unref(B);
    if (rc)
        return bail(nullptr, nullptr, nullptr);

    VipsImage *labTag = nullptr;
    if (vips_copy(joined, &labTag, "interpretation", VIPS_INTERPRETATION_LAB, nullptr))
        return bail(joined, nullptr, nullptr);
    g_object_unref(joined);

    VipsImage *srgb = nullptr;
    if (vips_colourspace(labTag, &srgb, VIPS_INTERPRETATION_sRGB, nullptr))
        return bail(labTag, nullptr, nullptr);
    g_object_unref(labTag);

    VipsImage *asFloat = nullptr;
    if (vips_cast(srgb, &asFloat, VIPS_FORMAT_FLOAT, nullptr))
        return bail(srgb, nullptr, nullptr);
    g_object_unref(srgb);
    VipsImage *result = nullptr;
    if (vips_copy(asFloat, &result, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr))
        return bail(asFloat, nullptr, nullptr);
    g_object_unref(asFloat);

    // 6. Re-attach the carried-through alpha.
    if (alpha) {
        VipsImage *withAlpha[2] = {result, alpha};
        VipsImage *full = nullptr;
        const int rc2 = vips_bandjoin(withAlpha, &full, 2, nullptr);
        g_object_unref(result);
        g_object_unref(alpha);
        if (rc2)
            return input;
        result = full;
    }
    return Image::adopt(result);
}

QJsonObject DenoiseNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("denoiseEnabled")] = m_values.enabled;
    state[QStringLiteral("luma")] = m_values.luma;
    state[QStringLiteral("chroma")] = m_values.chroma;
    return state;
}

void DenoiseNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    Values v;
    v.enabled = state.value(QStringLiteral("denoiseEnabled")).toBool(false);
    v.luma = static_cast<float>(state.value(QStringLiteral("luma")).toDouble(50.0));
    v.chroma = static_cast<float>(state.value(QStringLiteral("chroma")).toDouble(50.0));
    setValues(v);
}
