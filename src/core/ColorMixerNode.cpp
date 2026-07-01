// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/ColorMixerNode.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

ColorMixerNode::ColorMixerNode()
    : EditNode(QStringLiteral("colormixer"))
{
}

void ColorMixerNode::setValues(const ColorMixerValues &values)
{
    ColorMixerValues v = values;
    for (int i = 0; i < 8; ++i) {
        v.hue[i] = std::clamp(v.hue[i], kMinAmount, kMaxAmount);
        v.sat[i] = std::clamp(v.sat[i], kMinAmount, kMaxAmount);
        v.lum[i] = std::clamp(v.lum[i], kMinAmount, kMaxAmount);
    }
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

bool ColorMixerNode::isNeutral() const
{
    for (int i = 0; i < 8; ++i) {
        if (m_values.hue[i] != 0.0f || m_values.sat[i] != 0.0f || m_values.lum[i] != 0.0f)
            return false;
    }
    return true;
}

float ColorMixerNode::bandInterp(const float b[8], float hueDeg)
{
    // The 8 colours at their true hue angles (unevenly spaced). Linear-interpolate
    // between the two adjacent anchors containing `hueDeg` (the last segment wraps
    // 330°→360°≡0°). Identical to texture.frag's mixBandInterp and MonoNode.
    static const float c[8] = {0, 30, 60, 120, 180, 240, 270, 330};
    for (int i = 0; i < 8; ++i) {
        const float lo = c[i];
        const float hi = (i < 7) ? c[i + 1] : 360.0f;
        if (hueDeg >= lo && hueDeg < hi) {
            const float t = (hueDeg - lo) / (hi - lo);
            return b[i] * (1.0f - t) + b[(i + 1) & 7] * t;
        }
    }
    return b[0]; // unreachable (every hue in [0,360) lands in a segment)
}

void ColorMixerNode::rgbToHsv(float r, float g, float b, float &h, float &s, float &v)
{
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    v = mx;
    s = (mx <= 1e-6f) ? 0.0f : d / mx;
    if (d < 1e-6f) {
        h = 0.0f; // neutral; hue is irrelevant (saturation ~0)
        return;
    }
    float hh;
    if (mx == r)
        hh = std::fmod((g - b) / d, 6.0f);
    else if (mx == g)
        hh = (b - r) / d + 2.0f;
    else
        hh = (r - g) / d + 4.0f;
    hh *= 60.0f;
    if (hh < 0.0f)
        hh += 360.0f;
    h = hh;
}

void ColorMixerNode::hsvToRgb(float h, float s, float v, float &r, float &g, float &b)
{
    const float c = v * s;
    const float hp = h / 60.0f;
    const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r1, g1, b1;
    if (hp < 1.0f) {
        r1 = c; g1 = x; b1 = 0.0f;
    } else if (hp < 2.0f) {
        r1 = x; g1 = c; b1 = 0.0f;
    } else if (hp < 3.0f) {
        r1 = 0.0f; g1 = c; b1 = x;
    } else if (hp < 4.0f) {
        r1 = 0.0f; g1 = x; b1 = c;
    } else if (hp < 5.0f) {
        r1 = x; g1 = 0.0f; b1 = c;
    } else {
        r1 = c; g1 = 0.0f; b1 = x;
    }
    const float m = v - c;
    r = r1 + m;
    g = g1 + m;
    b = b1 + m;
}

void ColorMixerNode::contributeToPreview(PreviewState &state) const
{
    if (isNeutral())
        return; // passthrough → leave colorMixEnabled at its 0 default
    state.colorMixEnabled = 1.0f;
    state.mixHue0 = m_values.hue[0] / 100.0f;
    state.mixHue1 = m_values.hue[1] / 100.0f;
    state.mixHue2 = m_values.hue[2] / 100.0f;
    state.mixHue3 = m_values.hue[3] / 100.0f;
    state.mixHue4 = m_values.hue[4] / 100.0f;
    state.mixHue5 = m_values.hue[5] / 100.0f;
    state.mixHue6 = m_values.hue[6] / 100.0f;
    state.mixHue7 = m_values.hue[7] / 100.0f;
    state.mixSat0 = m_values.sat[0] / 100.0f;
    state.mixSat1 = m_values.sat[1] / 100.0f;
    state.mixSat2 = m_values.sat[2] / 100.0f;
    state.mixSat3 = m_values.sat[3] / 100.0f;
    state.mixSat4 = m_values.sat[4] / 100.0f;
    state.mixSat5 = m_values.sat[5] / 100.0f;
    state.mixSat6 = m_values.sat[6] / 100.0f;
    state.mixSat7 = m_values.sat[7] / 100.0f;
    state.mixLum0 = m_values.lum[0] / 100.0f;
    state.mixLum1 = m_values.lum[1] / 100.0f;
    state.mixLum2 = m_values.lum[2] / 100.0f;
    state.mixLum3 = m_values.lum[3] / 100.0f;
    state.mixLum4 = m_values.lum[4] / 100.0f;
    state.mixLum5 = m_values.lum[5] / 100.0f;
    state.mixLum6 = m_values.lum[6] / 100.0f;
    state.mixLum7 = m_values.lum[7] / 100.0f;
}

// Per-color HSL, per pixel, in encoded value space (0..255 → /255 → [0,1]).
// Byte-identical math to texture.frag's applyColorMix: RGB→HSV, interpolate the
// three band adjustments by hue, rotate hue / scale saturation & value, HSV→RGB,
// then blend the result in by the pixel's saturation (so greys stay put).
Image ColorMixerNode::apply(const Image &input) const
{
    if (input.isNull() || isNeutral())
        return input;
    if (std::min(vips_image_get_bands(input.handle()), 3) < 3)
        return input; // single-channel; no colour to mix

    float hb[8], sb[8], lb[8]; // slider/100 in [-1,1]
    for (int i = 0; i < 8; ++i) {
        hb[i] = m_values.hue[i] / 100.0f;
        sb[i] = m_values.sat[i] / 100.0f;
        lb[i] = m_values.lum[i] / 100.0f;
    }

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
        const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
        float hue, sat, val;
        rgbToHsv(r, g, b, hue, sat, val);
        if (sat < 1e-5f)
            continue; // neutral pixel: nothing to mix
        const float h2 =
            std::fmod(hue + bandInterp(hb, hue) * kHueRangeDeg + 360.0f, 360.0f);
        const float s2 = std::clamp(sat * (1.0f + bandInterp(sb, hue)), 0.0f, 1.0f);
        const float v2 =
            std::clamp(val * (1.0f + bandInterp(lb, hue) * kLumRange), 0.0f, 1.0f);
        float ar, ag, ab;
        hsvToRgb(h2, s2, v2, ar, ag, ab);
        p[0] = (r + (ar - r) * sat) * 255.0f;
        p[1] = (g + (ag - g) * sat) * 255.0f;
        p[2] = (b + (ab - b) * sat) * 255.0f;
        // alpha / extra bands untouched
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject ColorMixerNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    QJsonArray hue, sat, lum;
    for (int i = 0; i < 8; ++i) {
        hue.append(m_values.hue[i]);
        sat.append(m_values.sat[i]);
        lum.append(m_values.lum[i]);
    }
    state[QStringLiteral("mixHue")] = hue;
    state[QStringLiteral("mixSat")] = sat;
    state[QStringLiteral("mixLum")] = lum;
    return state;
}

void ColorMixerNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    ColorMixerValues v;
    const QJsonArray hue = state.value(QStringLiteral("mixHue")).toArray();
    const QJsonArray sat = state.value(QStringLiteral("mixSat")).toArray();
    const QJsonArray lum = state.value(QStringLiteral("mixLum")).toArray();
    for (int i = 0; i < 8; ++i) {
        v.hue[i] = static_cast<float>(hue.at(i).toDouble(0.0));
        v.sat[i] = static_cast<float>(sat.at(i).toDouble(0.0));
        v.lum[i] = static_cast<float>(lum.at(i).toDouble(0.0));
    }
    setValues(v);
}
