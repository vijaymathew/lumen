// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/HealNode.h"

#include "core/Inpaint.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

HealNode::HealNode()
    : EditNode(QStringLiteral("heal"))
{
}

void HealNode::setHealMask(const MaskBuffer &mask)
{
    m_mask = mask;
    invalidate();
}

Image HealNode::apply(const Image &input) const
{
    if (input.isNull() || m_mask.isEmpty())
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

    // The heal mask is painted at working resolution; upscale to the image.
    const MaskBuffer full = upscaleMask(m_mask, w, h);
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    bool any = false;
    for (size_t i = 0; i < mask.size(); ++i) {
        const uint8_t v = full.data[i] > 0.5f ? 255 : 0;
        mask[i] = v;
        any = any || v;
    }
    if (!any) {
        g_free(buf);
        return input;
    }

    auto *px = static_cast<uint8_t *>(buf);
    inpaintTelea(px, w, h, bands, mask, 5);

    Image result = Image::fromInterleaved(buf, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject HealNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    if (!m_mask.isEmpty())
        state[QStringLiteral("healMask")] = encodeMaskPng(m_mask);
    return state;
}

void HealNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    m_mask = decodeMaskPng(state.value(QStringLiteral("healMask")).toString());
}
