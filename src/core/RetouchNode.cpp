// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/RetouchNode.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

RetouchNode::RetouchNode()
    : EditNode(QStringLiteral("retouch"))
{
}

void RetouchNode::setPatches(const std::vector<Patch> &patches)
{
    m_patches = patches;
    invalidate();
}

void RetouchNode::setPatch(size_t index, uint8_t r, uint8_t g, uint8_t b, const MaskBuffer &mask)
{
    if (mask.isEmpty()) {
        if (index < m_patches.size())
            m_patches.erase(m_patches.begin() + static_cast<std::ptrdiff_t>(index));
    } else if (index < m_patches.size()) {
        m_patches[index] = {r, g, b, mask};
    } else {
        m_patches.push_back({r, g, b, mask});
    }
    invalidate();
}

void RetouchNode::setOpacity(int opacity)
{
    opacity = std::clamp(opacity, kMinOpacity, kMaxOpacity);
    if (opacity != m_opacity) {
        m_opacity = opacity;
        invalidate();
    }
}

void RetouchNode::applyToPixels(uint8_t *px, int w, int h, int bands) const
{
    if (!px || w <= 0 || h <= 0 || bands < 3)
        return;
    const float opacity = static_cast<float>(m_opacity) / kMaxOpacity;
    const size_t n = static_cast<size_t>(w) * h;
    for (const Patch &patch : m_patches) {
        if (patch.mask.isEmpty())
            continue;
        const MaskBuffer mask = upscaleMask(patch.mask, w, h);
        const float colour[3] = {static_cast<float>(patch.r), static_cast<float>(patch.g),
                                 static_cast<float>(patch.b)};
        for (size_t i = 0; i < n; ++i) {
            const float a = std::clamp(mask.data[i], 0.0f, 1.0f) * opacity;
            if (a <= 0.0f)
                continue;
            uint8_t *p = px + i * bands;
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<uint8_t>(std::lround(p[c] + (colour[c] - p[c]) * a));
        }
    }
}

Image RetouchNode::apply(const Image &input) const
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

QJsonObject RetouchNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    QJsonArray patches;
    for (const Patch &p : m_patches) {
        QJsonObject o;
        o[QStringLiteral("r")] = p.r;
        o[QStringLiteral("g")] = p.g;
        o[QStringLiteral("b")] = p.b;
        o[QStringLiteral("mask")] = encodeMaskPng(p.mask);
        patches.append(o);
    }
    state[QStringLiteral("patches")] = patches;
    state[QStringLiteral("opacity")] = m_opacity;
    return state;
}

void RetouchNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    m_patches.clear();
    for (const QJsonValue &v : state.value(QStringLiteral("patches")).toArray()) {
        const QJsonObject o = v.toObject();
        Patch p;
        p.r = static_cast<uint8_t>(std::clamp(o.value(QStringLiteral("r")).toInt(), 0, 255));
        p.g = static_cast<uint8_t>(std::clamp(o.value(QStringLiteral("g")).toInt(), 0, 255));
        p.b = static_cast<uint8_t>(std::clamp(o.value(QStringLiteral("b")).toInt(), 0, 255));
        p.mask = decodeMaskPng(o.value(QStringLiteral("mask")).toString());
        if (!p.mask.isEmpty())
            m_patches.push_back(std::move(p));
    }
    m_opacity = std::clamp(state.value(QStringLiteral("opacity")).toInt(kMaxOpacity),
                           kMinOpacity, kMaxOpacity);
    invalidate();
}
