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
    const auto wrapHue = [](float h) { return std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f); };
    v.shadowHue = wrapHue(v.shadowHue);
    v.highHue = wrapHue(v.highHue);
    v.shadowSat = std::clamp(v.shadowSat, 0.0f, 1.0f);
    v.highSat = std::clamp(v.highSat, 0.0f, 1.0f);
    v.balance = std::clamp(v.balance, -1.0f, 1.0f);
    for (float &bandValue : v.band)
        bandValue = std::clamp(bandValue, -1.0f, 1.0f);
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

void MonoNode::tintFromHue(float hueDeg, float satV, float &r, float &g, float &b)
{
    const QColor c = QColor::fromHsvF(std::clamp(hueDeg / 360.0f, 0.0f, 0.999f),
                                      std::clamp(satV, 0.0f, 1.0f), 1.0f);
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

float MonoNode::hue6(float r, float g, float b)
{
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    if (d < 1e-6f)
        return 0.0f; // neutral; chroma is ~0 so the hue is irrelevant
    float h;
    if (mx == r)
        h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;
    h *= 60.0f;
    if (h < 0.0f)
        h += 360.0f;
    return h;
}

float MonoNode::bandShift(const float band[8], float hueDeg)
{
    // The 8 colours at their true hue angles (unevenly spaced). Linear-interpolate
    // between the two adjacent anchors containing `hueDeg` (the last segment wraps
    // 330°→360°≡0°). Identical to texture.frag's monoBandShift.
    static const float c[8] = {0, 30, 60, 120, 180, 240, 270, 330};
    for (int i = 0; i < 8; ++i) {
        const float lo = c[i];
        const float hi = (i < 7) ? c[i + 1] : 360.0f;
        if (hueDeg >= lo && hueDeg < hi) {
            const float t = (hueDeg - lo) / (hi - lo);
            return band[i] * (1.0f - t) + band[(i + 1) & 7] * t;
        }
    }
    return band[0]; // unreachable (every hue in [0,360) lands in a segment)
}

void MonoNode::contributeToPreview(PreviewState &state) const
{
    if (!m_values.enabled)
        return; // passthrough → leave monoEnabled at its 0 default
    state.monoEnabled = 1.0f;
    normalizedWeights(state.monoR, state.monoG, state.monoB);
    state.monoBalance = m_values.balance;
    tintFromHue(m_values.shadowHue, m_values.shadowSat, state.monoShadowR, state.monoShadowG,
                state.monoShadowB);
    tintFromHue(m_values.highHue, m_values.highSat, state.monoHighR, state.monoHighG,
                state.monoHighB);
    state.monoBand0 = m_values.band[0];
    state.monoBand1 = m_values.band[1];
    state.monoBand2 = m_values.band[2];
    state.monoBand3 = m_values.band[3];
    state.monoBand4 = m_values.band[4];
    state.monoBand5 = m_values.band[5];
    state.monoBand6 = m_values.band[6];
    state.monoBand7 = m_values.band[7];
}

Image MonoNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled)
        return input;

    if (std::min(vips_image_get_bands(input.handle()), 3) < 3)
        return input; // already single-channel; nothing to mix

    float wr, wg, wb;
    normalizedWeights(wr, wg, wb);
    // Split-tone tints (luma-1 normalised; sat 0 → (1,1,1) = no tint), computed
    // once. Per pixel the grey blends between them by luminance (see the loop).
    float sh[3], hi[3];
    tintFromHue(m_values.shadowHue, m_values.shadowSat, sh[0], sh[1], sh[2]);
    tintFromHue(m_values.highHue, m_values.highSat, hi[0], hi[1], hi[2]);
    const float bal = m_values.balance;

    // The per-color mix is non-linear (depends on each pixel's hue), so this is a
    // single per-pixel pass — byte-identical math to texture.frag step 3.5.
    // Normalised [0,1] internally; values are 0..255 in the working float buffer.
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize, h = f->Ysize, bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;

    auto *px = static_cast<float *>(buf);
    const long long n = static_cast<long long>(w) * h;
    for (long long i = 0; i < n; ++i) {
        float *p = px + i * bands;
        const float nr = p[0] / 255.0f, ng = p[1] / 255.0f, nb = p[2] / 255.0f;
        float grey = wr * nr + wg * ng + wb * nb;            // base mix
        const float chroma = std::max({nr, ng, nb}) - std::min({nr, ng, nb});
        const float wHue = bandShift(m_values.band, hue6(nr, ng, nb));
        grey = std::clamp(grey * (1.0f + wHue * chroma * kBandGain), 0.0f, 1.0f);
        // Split toning: blend shadow→highlight tint by luminance (balance shifts
        // the crossover); tints are luma-1 so this preserves brightness.
        const float hw = std::clamp(grey + 0.5f * bal, 0.0f, 1.0f);
        p[0] = grey * (sh[0] + (hi[0] - sh[0]) * hw) * 255.0f;
        p[1] = grey * (sh[1] + (hi[1] - sh[1]) * hw) * 255.0f;
        p[2] = grey * (sh[2] + (hi[2] - sh[2]) * hw) * 255.0f;
        // alpha / extra bands untouched
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject MonoNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("monoEnabled")] = m_values.enabled;
    state[QStringLiteral("mixR")] = m_values.mixR;
    state[QStringLiteral("mixG")] = m_values.mixG;
    state[QStringLiteral("mixB")] = m_values.mixB;
    state[QStringLiteral("shadowHue")] = m_values.shadowHue;
    state[QStringLiteral("shadowSat")] = m_values.shadowSat;
    state[QStringLiteral("highHue")] = m_values.highHue;
    state[QStringLiteral("highSat")] = m_values.highSat;
    state[QStringLiteral("balance")] = m_values.balance;
    for (int i = 0; i < 8; ++i)
        state[QStringLiteral("band%1").arg(i)] = m_values.band[i];
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
    if (state.contains(QStringLiteral("shadowHue"))
        || state.contains(QStringLiteral("highHue"))) {
        v.shadowHue = static_cast<float>(state.value(QStringLiteral("shadowHue")).toDouble(0.0));
        v.shadowSat = static_cast<float>(state.value(QStringLiteral("shadowSat")).toDouble(0.0));
        v.highHue = static_cast<float>(state.value(QStringLiteral("highHue")).toDouble(0.0));
        v.highSat = static_cast<float>(state.value(QStringLiteral("highSat")).toDouble(0.0));
        v.balance = static_cast<float>(state.value(QStringLiteral("balance")).toDouble(0.0));
    } else if (state.contains(QStringLiteral("toneHue"))) {
        // Migrate the pre-split single tint: uniform tint = equal shadow/highlight,
        // amount = strength × saturation.
        const float hue = static_cast<float>(state.value(QStringLiteral("toneHue")).toDouble(32.0));
        const float strength =
            static_cast<float>(state.value(QStringLiteral("toneStrength")).toDouble(0.0));
        const float sat =
            static_cast<float>(state.value(QStringLiteral("toneSaturation")).toDouble(0.5));
        v.shadowHue = v.highHue = hue;
        v.shadowSat = v.highSat = std::clamp(strength * sat, 0.0f, 1.0f);
        v.balance = 0.0f;
    }
    for (int i = 0; i < 8; ++i)
        v.band[i] = static_cast<float>(
            state.value(QStringLiteral("band%1").arg(i)).toDouble(0.0));
    setValues(v);
}
