// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/TuneNode.h"

#include "core/WhiteBalance.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Rec. 709 luminance weights — must match texture.frag.
constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;

QJsonArray matToJson(const double m[9])
{
    QJsonArray a;
    for (int i = 0; i < 9; ++i)
        a.append(m[i]);
    return a;
}

void matFromJson(const QJsonArray &a, double m[9])
{
    if (a.size() != 9)
        return;
    for (int i = 0; i < 9; ++i)
        m[i] = a.at(i).toDouble(m[i]);
}
} // namespace

TuneNode::TuneNode()
    : EditNode(QStringLiteral("tune"))
{
    // No camera profile yet: the sRGB model stands in (camera = sRGB).
    std::copy(wb::kIdentity3, wb::kIdentity3 + 9, m_camToRgb);
    std::copy(wb::kSrgbXyzToRgb, wb::kSrgbXyzToRgb + 9, m_xyzToCam);
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

void TuneNode::setVibrance(float amount)
{
    amount = std::clamp(amount, kMinAmount, kMaxAmount);
    if (amount != m_vibrance) {
        m_vibrance = amount;
        invalidate();
    }
}

void TuneNode::setKelvin(float kelvin)
{
    kelvin = std::clamp(kelvin, kMinKelvin, kMaxKelvin);
    if (kelvin != m_kelvin) {
        m_kelvin = kelvin;
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

void TuneNode::setCameraProfile(const double camToRgb[9], const double xyzToCam[9],
                                const double asShotMul[3], bool seedKelvin)
{
    std::copy(camToRgb, camToRgb + 9, m_camToRgb);
    std::copy(xyzToCam, xyzToCam + 9, m_xyzToCam);
    m_hasProfile = true;
    m_asShotKelvin = static_cast<float>(
        std::clamp(wb::estimateKelvin(m_xyzToCam, asShotMul),
                   static_cast<double>(kMinKelvin), static_cast<double>(kMaxKelvin)));
    if (seedKelvin)
        m_kelvin = m_asShotKelvin;
    invalidate();
}

void TuneNode::whiteBalanceMatrix(double outW[9]) const
{
    wb::wbMatrix(m_camToRgb, m_xyzToCam, m_asShotKelvin, m_kelvin, m_tint, outW);
}

void TuneNode::pickNeutral(float r, float g, float b)
{
    // Linearise the sampled encoded pixel to match the WB transfer (^2.2).
    const double pl[3] = {
        std::pow(std::max(0.0, static_cast<double>(r)), 2.2),
        std::pow(std::max(0.0, static_cast<double>(g)), 2.2),
        std::pow(std::max(0.0, static_cast<double>(b)), 2.2),
    };
    double K = m_kelvin, t = m_tint;
    wb::solveNeutral(m_camToRgb, m_xyzToCam, m_asShotKelvin, pl, kMinKelvin, kMaxKelvin, K, t);
    setKelvin(static_cast<float>(K));
    setTint(static_cast<float>(t));
}

bool TuneNode::wbIsIdentity() const
{
    return m_kelvin == m_asShotKelvin && m_tint == 0.0f;
}

bool TuneNode::isNeutral() const
{
    return m_exposure == 0.0f && m_contrast == 0.0f && m_saturation == 0.0f
        && m_vibrance == 0.0f && wbIsIdentity();
}

void TuneNode::contributeToPreview(PreviewState &state) const
{
    // Same factor conversions the shader and apply() use.
    state.exposure += m_exposure;
    state.contrast *= 1.0f + m_contrast / 100.0f;
    state.saturation *= 1.0f + m_saturation / 100.0f;
    state.vibrance += m_vibrance / 100.0f; // additive amount; shader applies the curve

    // Accumulate the WB matrix: new = W_node · running (graph order, left-multiply).
    if (!wbIsIdentity()) {
        double W[9];
        whiteBalanceMatrix(W);
        const double cur[9] = {state.wb00, state.wb01, state.wb02,
                               state.wb10, state.wb11, state.wb12,
                               state.wb20, state.wb21, state.wb22};
        double res[9];
        wb::mat3Mul(W, cur, res);
        state.wb00 = static_cast<float>(res[0]);
        state.wb01 = static_cast<float>(res[1]);
        state.wb02 = static_cast<float>(res[2]);
        state.wb10 = static_cast<float>(res[3]);
        state.wb11 = static_cast<float>(res[4]);
        state.wb12 = static_cast<float>(res[5]);
        state.wb20 = static_cast<float>(res[6]);
        state.wb21 = static_cast<float>(res[7]);
        state.wb22 = static_cast<float>(res[8]);
    }
}

// Applies the WB matrix in linear light, matching the GPU shader exactly:
// encoded(0..255) → (v/255)^2.2 → W·rgb → ^(1/2.2) → ×255. Negatives are clamped
// to 0 before each power so saturated colours never produce NaNs. Alpha (and any
// extra band) is left untouched.
Image TuneNode::applyWhiteBalance(const Image &input, const double W[9]) const
{
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize;
    const int h = f->Ysize;
    const int bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;

    auto *px = static_cast<float *>(buf);
    const long long n = static_cast<long long>(w) * h;
    for (long long i = 0; i < n; ++i) {
        float *p = px + i * bands;
        double lin[3];
        for (int c = 0; c < 3; ++c) {
            const double v = std::max(0.0, p[c] / 255.0);
            lin[c] = std::pow(v, 2.2);
        }
        double out[3];
        wb::mat3MulVec(W, lin, out);
        for (int c = 0; c < 3; ++c)
            p[c] = static_cast<float>(std::pow(std::max(0.0, out[c]), 1.0 / 2.2) * 255.0);
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

// Saturation-aware vibrance, per pixel, in encoded value space — identical math
// to texture.frag's applyTone vibrance block. `vib` is the slider/100 in [-1,1].
// Low-saturation pixels get the strongest push; already-saturated ones barely
// change. No clamp (matches the saturation step); export quantises at the end.
Image TuneNode::applyVibrance(const Image &input, double vib) const
{
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize;
    const int h = f->Ysize;
    const int bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;
    if (bands < 3) { // grayscale: nothing to do
        g_free(buf);
        return input;
    }

    auto *px = static_cast<float *>(buf);
    const long long n = static_cast<long long>(w) * h;
    for (long long i = 0; i < n; ++i) {
        float *p = px + i * bands;
        const double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
        const double mx = std::max(r, std::max(g, b));
        const double mn = std::min(r, std::min(g, b));
        const double sat = mx - mn;                          // [0,1]
        const double fac = std::max(0.0, 1.0 + vib * (1.0 - sat));
        const double l = kLumaR * r + kLumaG * g + kLumaB * b;
        p[0] = static_cast<float>((l + (r - l) * fac) * 255.0);
        p[1] = static_cast<float>((l + (g - l) * fac) * 255.0);
        p[2] = static_cast<float>((l + (b - l) * fac) * 255.0);
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject TuneNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("exposure")] = m_exposure;
    state[QStringLiteral("contrast")] = m_contrast;
    state[QStringLiteral("saturation")] = m_saturation;
    state[QStringLiteral("vibrance")] = m_vibrance;
    state[QStringLiteral("kelvin")] = m_kelvin;
    state[QStringLiteral("tint")] = m_tint;
    state[QStringLiteral("asShotKelvin")] = m_asShotKelvin;
    if (m_hasProfile) {
        state[QStringLiteral("camToRgb")] = matToJson(m_camToRgb);
        state[QStringLiteral("xyzToCam")] = matToJson(m_xyzToCam);
    }
    return state;
}

void TuneNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    setExposure(static_cast<float>(state.value(QStringLiteral("exposure")).toDouble()));
    setContrast(static_cast<float>(state.value(QStringLiteral("contrast")).toDouble()));
    setSaturation(static_cast<float>(state.value(QStringLiteral("saturation")).toDouble()));
    setVibrance(static_cast<float>(state.value(QStringLiteral("vibrance")).toDouble()));

    // Camera profile (RAW projects); leave the sRGB defaults otherwise.
    if (state.contains(QStringLiteral("camToRgb"))) {
        matFromJson(state.value(QStringLiteral("camToRgb")).toArray(), m_camToRgb);
        matFromJson(state.value(QStringLiteral("xyzToCam")).toArray(), m_xyzToCam);
        m_hasProfile = true;
    }
    m_asShotKelvin = static_cast<float>(
        state.value(QStringLiteral("asShotKelvin")).toDouble(kDefaultKelvin));
    // "kelvin" is the WB v2 key; old projects (encoded "temperature") have none,
    // so default to the as-shot temperature (a neutral WB matrix).
    setKelvin(static_cast<float>(
        state.value(QStringLiteral("kelvin")).toDouble(m_asShotKelvin)));
    setTint(static_cast<float>(state.value(QStringLiteral("tint")).toDouble()));
}

Image TuneNode::apply(const Image &input) const
{
    if (input.isNull() || isNeutral())
        return input;

    // 0. White balance — a 3x3 matrix in linear light, before everything else
    //    (matches the shader: WB then applyTone).
    Image work = input;
    if (!wbIsIdentity()) {
        double W[9];
        whiteBalanceMatrix(W);
        work = applyWhiteBalance(input, W);
    }

    // Factors (identical to contributeToPreview / the shader).
    const double f = std::pow(2.0, static_cast<double>(m_exposure) / 2.2); // exposure
    const double c = 1.0 + static_cast<double>(m_contrast) / 100.0;        // contrast
    const double s = 1.0 + static_cast<double>(m_saturation) / 100.0;      // saturation

    VipsImage *cur = work.handle();
    g_object_ref(cur); // own a ref through the chain
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };

    const int bands = vips_image_get_bands(cur);
    const int colorBands = std::min(bands, 3);

    // 1. Exposure + contrast as a single per-band affine, in 8-bit value space:
    //    out = c*f*v + 127.5*(1-c). (Encoded space; pivot mid-grey.)
    if (m_exposure != 0.0f || m_contrast != 0.0f) {
        std::vector<double> a(bands, 1.0);
        std::vector<double> b(bands, 0.0);
        const double pivot = 127.5 * (1.0 - c);
        for (int i = 0; i < colorBands; ++i) {
            a[i] = c * f;
            b[i] = pivot;
        }
        VipsImage *lin = nullptr;
        if (vips_linear(cur, &lin, a.data(), b.data(), bands, nullptr)) {
            g_object_unref(cur);
            return work;
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
            return work;
        }
        VipsImage *recombined = nullptr;
        const int rc = vips_recomb(cur, &recombined, matrix, nullptr);
        g_object_unref(matrix);
        if (rc) {
            g_object_unref(cur);
            return work;
        }
        replace(recombined);
    }

    // Keep the result in the float working format (vips_linear/recomb already
    // promoted to float); the pipeline quantises only at display/export.
    Image toned = Image::adopt(cur);

    // 3. Vibrance: saturation-aware, per pixel, after saturation (matches the
    //    shader order). Done last among the pointwise tone ops.
    if (m_vibrance != 0.0f)
        return applyVibrance(toned, static_cast<double>(m_vibrance) / 100.0);
    return toned;
}
