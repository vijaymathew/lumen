// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/SelectiveNode.h"

#include "core/SelectiveMask.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kLumaR = 0.2126;
constexpr double kLumaG = 0.7152;
constexpr double kLumaB = 0.0722;
constexpr float kMinFeather = 0.001f;

double smoothstep(double e0, double e1, double x)
{
    if (e0 == e1)
        return x < e0 ? 0.0 : 1.0;
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
} // namespace

SelectiveNode::SelectiveNode()
    : EditNode(QStringLiteral("selective"))
{
}

void SelectiveNode::setValues(const SelectiveValues &values)
{
    SelectiveValues v = values;
    v.maskMode = std::clamp(v.maskMode, 0, 1);
    v.low = std::clamp(v.low, 0.0f, 1.0f);
    v.high = std::clamp(v.high, 0.0f, 1.0f);
    if (v.high < v.low)
        std::swap(v.low, v.high);
    v.feather = std::clamp(v.feather, kMinFeather, 1.0f);
    v.targetR = std::clamp(v.targetR, 0.0f, 1.0f);
    v.targetG = std::clamp(v.targetG, 0.0f, 1.0f);
    v.targetB = std::clamp(v.targetB, 0.0f, 1.0f);
    v.colorRange = std::clamp(v.colorRange, 0.02f, 1.0f);
    v.exposure = std::clamp(v.exposure, kMinExposure, kMaxExposure);
    v.contrast = std::clamp(v.contrast, kMinAmount, kMaxAmount);
    v.saturation = std::clamp(v.saturation, kMinAmount, kMaxAmount);

    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

bool SelectiveNode::isNeutral() const
{
    return m_values.exposure == 0.0f && m_values.contrast == 0.0f
        && m_values.saturation == 0.0f;
}

void SelectiveNode::contributeToPreview(PreviewState &state) const
{
    state.selMaskMode = static_cast<float>(m_values.maskMode);
    // Publish the luminosity range so the mask overlay works even before an
    // adjustment is dialled in (colour-mask preview comes from a texture).
    state.selLow = m_values.low;
    state.selHigh = m_values.high;
    state.selFeather = std::max(m_values.feather, kMinFeather);

    if (isNeutral())
        return;
    state.selEnabled = 1.0f;
    state.selExposure = m_values.exposure;
    state.selContrast = 1.0f + m_values.contrast / 100.0f;
    state.selSaturation = 1.0f + m_values.saturation / 100.0f;
}

Image SelectiveNode::apply(const Image &input) const
{
    if (input.isNull() || isNeutral())
        return input;

    VipsImage *u8 = nullptr;
    if (vips_cast(input.handle(), &u8, VIPS_FORMAT_UCHAR, nullptr))
        return input;

    size_t size = 0;
    void *buf = vips_image_write_to_memory(u8, &size);
    const int w = u8->Xsize;
    const int h = u8->Ysize;
    const int bands = u8->Bands;
    g_object_unref(u8);
    if (!buf)
        return input;

    auto *px = static_cast<uint8_t *>(buf);

    const bool colorMode = m_values.maskMode == 1;
    MaskBuffer colorMask;
    if (colorMode)
        colorMask = colorAffinityMask(px, w, h, bands, m_values.targetR,
                                      m_values.targetG, m_values.targetB,
                                      m_values.colorRange);

    const double low = m_values.low;
    const double high = m_values.high;
    const double feather = std::max(m_values.feather, kMinFeather);
    const double f = std::pow(2.0, static_cast<double>(m_values.exposure) / 2.2);
    const double c = 1.0 + m_values.contrast / 100.0;
    const double s = 1.0 + m_values.saturation / 100.0;

    const long long n = static_cast<long long>(w) * h;
    for (long long i = 0; i < n; ++i) {
        uint8_t *p = px + i * bands;
        double col[3] = {p[0] / 255.0, p[1] / 255.0, p[2] / 255.0};

        double mask;
        if (colorMode) {
            mask = colorMask.data.empty() ? 0.0 : colorMask.data[i];
        } else {
            const double L = kLumaR * col[0] + kLumaG * col[1] + kLumaB * col[2];
            mask = smoothstep(low - feather, low, L)
                 * (1.0 - smoothstep(high, high + feather, L));
        }

        double adj[3] = {col[0] * f, col[1] * f, col[2] * f};
        for (int ch = 0; ch < 3; ++ch)
            adj[ch] = (adj[ch] - 0.5) * c + 0.5;
        const double al = kLumaR * adj[0] + kLumaG * adj[1] + kLumaB * adj[2];
        for (int ch = 0; ch < 3; ++ch)
            adj[ch] = al + (adj[ch] - al) * s;

        for (int ch = 0; ch < 3; ++ch) {
            const double out = col[ch] * (1.0 - mask) + adj[ch] * mask;
            p[ch] = static_cast<uint8_t>(std::clamp(std::lround(out * 255.0), 0L, 255L));
        }
    }

    VipsImage *result = vips_image_new_from_memory_copy(buf, size, w, h, bands,
                                                        VIPS_FORMAT_UCHAR);
    g_free(buf);
    if (!result)
        return input;
    return Image::adopt(result);
}

QJsonObject SelectiveNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("maskMode")] = m_values.maskMode;
    state[QStringLiteral("low")] = m_values.low;
    state[QStringLiteral("high")] = m_values.high;
    state[QStringLiteral("feather")] = m_values.feather;
    state[QStringLiteral("targetR")] = m_values.targetR;
    state[QStringLiteral("targetG")] = m_values.targetG;
    state[QStringLiteral("targetB")] = m_values.targetB;
    state[QStringLiteral("colorRange")] = m_values.colorRange;
    state[QStringLiteral("exposure")] = m_values.exposure;
    state[QStringLiteral("contrast")] = m_values.contrast;
    state[QStringLiteral("saturation")] = m_values.saturation;
    return state;
}

void SelectiveNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    SelectiveValues v;
    v.maskMode = state.value(QStringLiteral("maskMode")).toInt(0);
    v.low = static_cast<float>(state.value(QStringLiteral("low")).toDouble(0.0));
    v.high = static_cast<float>(state.value(QStringLiteral("high")).toDouble(1.0));
    v.feather = static_cast<float>(state.value(QStringLiteral("feather")).toDouble(0.1));
    v.targetR = static_cast<float>(state.value(QStringLiteral("targetR")).toDouble(0.0));
    v.targetG = static_cast<float>(state.value(QStringLiteral("targetG")).toDouble(0.0));
    v.targetB = static_cast<float>(state.value(QStringLiteral("targetB")).toDouble(0.0));
    v.colorRange = static_cast<float>(state.value(QStringLiteral("colorRange")).toDouble(0.3));
    v.exposure = static_cast<float>(state.value(QStringLiteral("exposure")).toDouble(0.0));
    v.contrast = static_cast<float>(state.value(QStringLiteral("contrast")).toDouble(0.0));
    v.saturation = static_cast<float>(state.value(QStringLiteral("saturation")).toDouble(0.0));
    setValues(v);
}
