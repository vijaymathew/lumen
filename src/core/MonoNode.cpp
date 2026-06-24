// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/MonoNode.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Rec. 709 luminance weights — must match texture.frag.
constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;
} // namespace

MonoNode::MonoNode()
    : EditNode(QStringLiteral("mono"))
{
}

void MonoNode::setValues(const MonoValues &values)
{
    MonoValues v = values;
    v.mixR = std::clamp(v.mixR, -2.0f, 2.0f);
    v.mixG = std::clamp(v.mixG, -2.0f, 2.0f);
    v.mixB = std::clamp(v.mixB, -2.0f, 2.0f);
    v.toneStrength = std::clamp(v.toneStrength, 0.0f, 1.0f);
    v.toneHue = std::fmod(std::fmod(v.toneHue, 360.0f) + 360.0f, 360.0f);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

void MonoNode::normalizedWeights(float &r, float &g, float &b) const
{
    const float sum = m_values.mixR + m_values.mixG + m_values.mixB;
    if (std::abs(sum) < 1e-4f) { // degenerate mix → fall back to luma
        r = static_cast<float>(kLumaR);
        g = static_cast<float>(kLumaG);
        b = static_cast<float>(kLumaB);
        return;
    }
    r = m_values.mixR / sum;
    g = m_values.mixG / sum;
    b = m_values.mixB / sum;
}

void MonoNode::tintFromHue(float hueDeg, float &r, float &g, float &b)
{
    const QColor c = QColor::fromHsvF(std::clamp(hueDeg / 360.0f, 0.0f, 0.999f),
                                      kToneSaturation, 1.0f);
    double tr = c.redF(), tg = c.greenF(), tb = c.blueF();
    const double luma = kLumaR * tr + kLumaG * tg + kLumaB * tb;
    if (luma > 1e-4) {
        tr /= luma;
        tg /= luma;
        tb /= luma;
    }
    r = static_cast<float>(tr);
    g = static_cast<float>(tg);
    b = static_cast<float>(tb);
}

void MonoNode::contributeToPreview(PreviewState &state) const
{
    if (!m_values.enabled)
        return; // passthrough → leave monoEnabled at its 0 default
    state.monoEnabled = 1.0f;
    normalizedWeights(state.monoR, state.monoG, state.monoB);
    state.monoToneStrength = m_values.toneStrength;
    tintFromHue(m_values.toneHue, state.monoToneR, state.monoToneG, state.monoToneB);
}

Image MonoNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled)
        return input;

    const int bands = vips_image_get_bands(input.handle());
    const int colorBands = std::min(bands, 3);
    if (colorBands < 3)
        return input; // already single-channel; nothing to mix

    float wr, wg, wb;
    normalizedWeights(wr, wg, wb);
    float tr, tg, tb;
    tintFromHue(m_values.toneHue, tr, tg, tb);
    const double s = m_values.toneStrength;
    // Per-channel toning factor: out_i = grey * mix(1, tint_i, strength), which
    // reproduces mix(vec3(grey), grey*tint, strength) exactly (as in the shader).
    const double k[3] = {1.0 + s * (tr - 1.0), 1.0 + s * (tg - 1.0),
                         1.0 + s * (tb - 1.0)};

    VipsImage *cur = input.handle();
    g_object_ref(cur);
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };

    // 1. Recombine every colour channel to the same grey = w·rgb (alpha/extra
    //    bands pass through).
    std::vector<double> m(static_cast<size_t>(bands) * bands, 0.0);
    const double w[3] = {wr, wg, wb};
    for (int i = 0; i < bands; ++i) {
        for (int j = 0; j < bands; ++j) {
            double v;
            if (i < 3 && j < 3)
                v = w[j]; // each colour output row is the same grey mix
            else
                v = (i == j ? 1.0 : 0.0); // pass alpha / extra bands
            m[static_cast<size_t>(i) * bands + j] = v;
        }
    }
    VipsImage *matrix =
        vips_image_new_matrix_from_array(bands, bands, m.data(), bands * bands);
    if (!matrix) {
        g_object_unref(cur);
        return input;
    }
    VipsImage *grey = nullptr;
    const int rc = vips_recomb(cur, &grey, matrix, nullptr);
    g_object_unref(matrix);
    if (rc) {
        g_object_unref(cur);
        return input;
    }
    replace(grey);

    // 2. Toning: scale each colour channel by k_i (alpha untouched).
    if (s > 0.0) {
        std::vector<double> a(bands, 1.0), b(bands, 0.0);
        for (int i = 0; i < colorBands; ++i)
            a[i] = k[i];
        VipsImage *toned = nullptr;
        if (vips_linear(cur, &toned, a.data(), b.data(), bands, nullptr)) {
            g_object_unref(cur);
            return input;
        }
        replace(toned);
    }

    // Keep the result in the float working format (recomb/linear already
    // promoted to float); the pipeline quantises only at display/export.
    return Image::adopt(cur);
}

QJsonObject MonoNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("monoEnabled")] = m_values.enabled;
    state[QStringLiteral("mixR")] = m_values.mixR;
    state[QStringLiteral("mixG")] = m_values.mixG;
    state[QStringLiteral("mixB")] = m_values.mixB;
    state[QStringLiteral("toneStrength")] = m_values.toneStrength;
    state[QStringLiteral("toneHue")] = m_values.toneHue;
    return state;
}

void MonoNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    MonoValues v;
    v.enabled = state.value(QStringLiteral("monoEnabled")).toBool(false);
    v.mixR = static_cast<float>(state.value(QStringLiteral("mixR")).toDouble(0.2126));
    v.mixG = static_cast<float>(state.value(QStringLiteral("mixG")).toDouble(0.7152));
    v.mixB = static_cast<float>(state.value(QStringLiteral("mixB")).toDouble(0.0722));
    v.toneStrength =
        static_cast<float>(state.value(QStringLiteral("toneStrength")).toDouble(0.0));
    v.toneHue = static_cast<float>(state.value(QStringLiteral("toneHue")).toDouble(32.0));
    setValues(v);
}
