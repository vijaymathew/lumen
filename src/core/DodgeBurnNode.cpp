// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/DodgeBurnNode.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace {
// Rec.709 luma of encoded RGB in [0,1].
float lumaOf(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}
} // namespace

DodgeBurnNode::DodgeBurnNode()
    : EditNode(QStringLiteral("dodgeburn"))
{
}

void DodgeBurnNode::setDodgeMask(const MaskBuffer &mask)
{
    m_dodge = mask;
    invalidate();
}

void DodgeBurnNode::setBurnMask(const MaskBuffer &mask)
{
    m_burn = mask;
    invalidate();
}

void DodgeBurnNode::setExposure(int exposure)
{
    exposure = std::clamp(exposure, kMinExposure, kMaxExposure);
    if (exposure != m_exposure) {
        m_exposure = exposure;
        invalidate();
    }
}

void DodgeBurnNode::setRange(Range range)
{
    if (range != m_range) {
        m_range = range;
        invalidate();
    }
}

float DodgeBurnNode::rangeWeight(Range range, float luma)
{
    const float l = std::clamp(luma, 0.0f, 1.0f);
    switch (range) {
    case Range::Shadows:
        return (1.0f - l) * (1.0f - l);
    case Range::Highlights:
        return l * l;
    case Range::Midtones:
        break;
    }
    return 4.0f * l * (1.0f - l);
}

void DodgeBurnNode::applyToPixels(uint8_t *px, int w, int h, int bands) const
{
    if (!px || w <= 0 || h <= 0 || bands < 3 || !hasEffect())
        return;
    const MaskBuffer dodge = m_dodge.isEmpty() ? MaskBuffer() : upscaleMask(m_dodge, w, h);
    const MaskBuffer burn = m_burn.isEmpty() ? MaskBuffer() : upscaleMask(m_burn, w, h);
    const float ev = kMaxEv * static_cast<float>(m_exposure) / kMaxExposure;

    for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) {
        const float net = (dodge.isEmpty() ? 0.0f : dodge.data[i])
                        - (burn.isEmpty() ? 0.0f : burn.data[i]);
        if (net == 0.0f)
            continue;
        uint8_t *p = px + i * bands;
        const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
        // Same encoded-space exposure model as the Tone panel (2.2 gamma), so a
        // given EV looks the same here as there.
        const float gain = std::exp2(ev * net * rangeWeight(m_range, lumaOf(r, g, b)) / 2.2f);
        p[0] = static_cast<uint8_t>(std::lround(std::clamp(r * gain, 0.0f, 1.0f) * 255.0f));
        p[1] = static_cast<uint8_t>(std::lround(std::clamp(g * gain, 0.0f, 1.0f) * 255.0f));
        p[2] = static_cast<uint8_t>(std::lround(std::clamp(b * gain, 0.0f, 1.0f) * 255.0f));
    }
}

Image DodgeBurnNode::apply(const Image &input) const
{
    if (input.isNull() || !hasEffect())
        return input;

    VipsImage *u8 = nullptr;
    if (vips_cast(input.handle(), &u8, VIPS_FORMAT_UCHAR, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(u8, nullptr);
    const int w = u8->Xsize;
    const int h = u8->Ysize;
    const int bands = u8->Bands;
    g_object_unref(u8);
    if (!buf)
        return input;

    applyToPixels(static_cast<uint8_t *>(buf), w, h, bands);
    Image result = Image::fromInterleaved(buf, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject DodgeBurnNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    if (!m_dodge.isEmpty())
        state[QStringLiteral("dodgeMask")] = encodeMaskPng(m_dodge);
    if (!m_burn.isEmpty())
        state[QStringLiteral("burnMask")] = encodeMaskPng(m_burn);
    state[QStringLiteral("exposure")] = m_exposure;
    state[QStringLiteral("range")] = static_cast<int>(m_range);
    return state;
}

void DodgeBurnNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    m_dodge = decodeMaskPng(state.value(QStringLiteral("dodgeMask")).toString());
    m_burn = decodeMaskPng(state.value(QStringLiteral("burnMask")).toString());
    m_exposure = std::clamp(state.value(QStringLiteral("exposure")).toInt(kDefaultExposure),
                            kMinExposure, kMaxExposure);
    m_range = static_cast<Range>(std::clamp(
        state.value(QStringLiteral("range")).toInt(static_cast<int>(Range::Midtones)), 0, 2));
    invalidate();
}
