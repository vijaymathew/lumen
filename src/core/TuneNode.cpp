// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/TuneNode.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Rec. 709 luminance weights — must match texture.frag.
constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;
} // namespace

TuneNode::TuneNode()
    : EditNode(QStringLiteral("tune"))
{
}

void TuneNode::setExposure(float ev)
{
    ev = std::clamp(ev, kMinExposure, kMaxExposure);
    if (ev != m_exposure) {
        m_exposure = ev;
        invalidate();
    }
}

void TuneNode::setContrast(float amount)
{
    amount = std::clamp(amount, kMinAmount, kMaxAmount);
    if (amount != m_contrast) {
        m_contrast = amount;
        invalidate();
    }
}

void TuneNode::setSaturation(float amount)
{
    amount = std::clamp(amount, kMinAmount, kMaxAmount);
    if (amount != m_saturation) {
        m_saturation = amount;
        invalidate();
    }
}

void TuneNode::setTemperature(float amount)
{
    amount = std::clamp(amount, kMinAmount, kMaxAmount);
    if (amount != m_temperature) {
        m_temperature = amount;
        invalidate();
    }
}

void TuneNode::setTint(float amount)
{
    amount = std::clamp(amount, kMinAmount, kMaxAmount);
    if (amount != m_tint) {
        m_tint = amount;
        invalidate();
    }
}

void TuneNode::wbGains(float temperature, float tint, float &r, float &g, float &b)
{
    const float t = temperature / 100.0f;  // -1..1, warm(+)/cool(-)
    const float ti = tint / 100.0f;         // -1..1, magenta(+)/green(-)
    // Temperature is the R↔B axis; tint is mainly G, with a small R/B nudge so
    // magenta/green read cleanly. Neutral (0,0) → unit gains.
    r = 1.0f + 0.30f * t + 0.10f * ti;
    g = 1.0f - 0.20f * ti;
    b = 1.0f - 0.30f * t + 0.10f * ti;
}

bool TuneNode::isNeutral() const
{
    return m_exposure == 0.0f && m_contrast == 0.0f && m_saturation == 0.0f
        && m_temperature == 0.0f && m_tint == 0.0f;
}

void TuneNode::contributeToPreview(PreviewState &state) const
{
    // Same factor conversions the shader and apply() use.
    state.exposure += m_exposure;
    state.contrast *= 1.0f + m_contrast / 100.0f;
    state.saturation *= 1.0f + m_saturation / 100.0f;
    float gr, gg, gb;
    wbGains(m_temperature, m_tint, gr, gg, gb);
    state.wbR *= gr;
    state.wbG *= gg;
    state.wbB *= gb;
}

QJsonObject TuneNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("exposure")] = m_exposure;
    state[QStringLiteral("contrast")] = m_contrast;
    state[QStringLiteral("saturation")] = m_saturation;
    state[QStringLiteral("temperature")] = m_temperature;
    state[QStringLiteral("tint")] = m_tint;
    return state;
}

void TuneNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    setExposure(static_cast<float>(state.value(QStringLiteral("exposure")).toDouble()));
    setContrast(static_cast<float>(state.value(QStringLiteral("contrast")).toDouble()));
    setSaturation(static_cast<float>(state.value(QStringLiteral("saturation")).toDouble()));
    setTemperature(static_cast<float>(state.value(QStringLiteral("temperature")).toDouble()));
    setTint(static_cast<float>(state.value(QStringLiteral("tint")).toDouble()));
}

Image TuneNode::apply(const Image &input) const
{
    if (input.isNull() || isNeutral())
        return input;

    // Factors (identical to contributeToPreview / the shader).
    const double f = std::pow(2.0, static_cast<double>(m_exposure) / 2.2); // exposure
    const double c = 1.0 + static_cast<double>(m_contrast) / 100.0;        // contrast
    const double s = 1.0 + static_cast<double>(m_saturation) / 100.0;      // saturation

    VipsImage *cur = input.handle();
    g_object_ref(cur); // own a ref through the chain
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };

    const int bands = vips_image_get_bands(cur);
    const int colorBands = std::min(bands, 3);

    // White-balance per-channel gains (applied before exposure, matching the
    // shader's `col *= wb` ahead of applyTone).
    float gr = 1.0f, gg = 1.0f, gb = 1.0f;
    wbGains(m_temperature, m_tint, gr, gg, gb);
    const double gain[3] = {gr, gg, gb};
    const bool wbActive = m_temperature != 0.0f || m_tint != 0.0f;

    // 1. WB + exposure + contrast as a single per-band affine, in 8-bit value
    //    space: out = c*f*gain*v + 127.5*(1-c). (Encoded space; pivot mid-grey.)
    if (m_exposure != 0.0f || m_contrast != 0.0f || wbActive) {
        std::vector<double> a(bands, 1.0);
        std::vector<double> b(bands, 0.0);
        const double pivot = 127.5 * (1.0 - c);
        for (int i = 0; i < colorBands; ++i) {
            a[i] = c * f * gain[i];
            b[i] = pivot;
        }
        VipsImage *lin = nullptr;
        if (vips_linear(cur, &lin, a.data(), b.data(), bands, nullptr)) {
            g_object_unref(cur);
            return input;
        }
        replace(lin);
    }

    // 2. Saturation: recombine RGB toward luma. out_i = s*v_i + (1-s)*luma.
    if (m_saturation != 0.0f && colorBands == 3) {
        const double w[3] = {kLumaR, kLumaG, kLumaB};
        std::vector<double> m(static_cast<size_t>(bands) * bands, 0.0);
        for (int i = 0; i < bands; ++i) {
            for (int j = 0; j < bands; ++j) {
                double v;
                if (i < 3 && j < 3)
                    v = (i == j ? s : 0.0) + (1.0 - s) * w[j];
                else
                    v = (i == j ? 1.0 : 0.0); // pass alpha (and any extra band)
                m[static_cast<size_t>(i) * bands + j] = v;
            }
        }
        VipsImage *matrix = vips_image_new_matrix_from_array(bands, bands, m.data(),
                                                             bands * bands);
        if (!matrix) {
            g_object_unref(cur);
            return input;
        }
        VipsImage *recombined = nullptr;
        const int rc = vips_recomb(cur, &recombined, matrix, nullptr);
        g_object_unref(matrix);
        if (rc) {
            g_object_unref(cur);
            return input;
        }
        replace(recombined);
    }

    // Keep the result in the float working format (vips_linear/recomb already
    // promoted to float); the pipeline quantises only at display/export.
    return Image::adopt(cur);
}
