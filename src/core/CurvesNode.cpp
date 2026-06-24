// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/CurvesNode.h"

#include <QJsonArray>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

QJsonArray curveToJson(const Curve &curve)
{
    QJsonArray pts;
    for (const QPointF &p : curve.points()) {
        QJsonArray xy;
        xy.append(p.x());
        xy.append(p.y());
        pts.append(xy);
    }
    return pts;
}

Curve curveFromJson(const QJsonValue &value)
{
    std::vector<QPointF> points;
    const QJsonArray pts = value.toArray();
    points.reserve(pts.size());
    for (const QJsonValue &v : pts) {
        const QJsonArray xy = v.toArray();
        if (xy.size() == 2)
            points.emplace_back(xy[0].toDouble(), xy[1].toDouble());
    }
    Curve c;
    if (points.size() >= 2)
        c.setPoints(std::move(points));
    return c;
}

} // namespace

CurvesNode::CurvesNode()
    : EditNode(QStringLiteral("curves"))
{
}

void CurvesNode::setCurves(const ChannelCurves &curves)
{
    m_curves = curves;
    invalidate();
}

bool CurvesNode::isIdentity() const
{
    return m_curves.master.isIdentity() && m_curves.red.isIdentity()
        && m_curves.green.isIdentity() && m_curves.blue.isIdentity();
}

std::array<uint8_t, 256> CurvesNode::effectiveLut(int channel) const
{
    const std::array<uint8_t, 256> masterLut = m_curves.master.buildLut();
    const Curve &chan = channel == 0 ? m_curves.red
                      : channel == 1 ? m_curves.green
                                     : m_curves.blue;
    const std::array<uint8_t, 256> chanLut = chan.buildLut();

    std::array<uint8_t, 256> eff{};
    for (int i = 0; i < 256; ++i)
        eff[i] = chanLut[masterLut[i]]; // master first, then channel
    return eff;
}

void CurvesNode::contributeToPreviewLut(ChannelLuts &luts) const
{
    if (isIdentity())
        return;
    for (int c = 0; c < 3; ++c) {
        const std::array<uint8_t, 256> eff = effectiveLut(c);
        for (int i = 0; i < 256; ++i)
            luts[c][i] = eff[luts[c][i]];
    }
}

Image CurvesNode::apply(const Image &input) const
{
    if (input.isNull() || isIdentity())
        return input;

    const std::array<uint8_t, 256> lut[3] = {effectiveLut(0), effectiveLut(1),
                                             effectiveLut(2)};

    // The working image is float sRGB (0..255). Cast to float, then apply each
    // channel's curve by *interpolating* its 256-entry LUT — matching the GPU's
    // linear LUT-texture sampling (and preserving precision, unlike 8-bit maplut).
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize, h = f->Ysize, bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;

    const auto curveAt = [](const std::array<uint8_t, 256> &c, float v) {
        v = std::clamp(v, 0.0f, 255.0f);
        const int i0 = static_cast<int>(v);
        const int i1 = std::min(i0 + 1, 255);
        const float frac = v - static_cast<float>(i0);
        return c[i0] * (1.0f - frac) + c[i1] * frac;
    };

    auto *px = static_cast<float *>(buf);
    const long long n = static_cast<long long>(w) * h;
    const int colour = std::min(bands, 3);
    for (long long i = 0; i < n; ++i) {
        float *p = px + i * bands;
        for (int ch = 0; ch < colour; ++ch)
            p[ch] = curveAt(lut[ch], p[ch]);
        // alpha (and any extra band) untouched
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject CurvesNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("master")] = curveToJson(m_curves.master);
    state[QStringLiteral("red")] = curveToJson(m_curves.red);
    state[QStringLiteral("green")] = curveToJson(m_curves.green);
    state[QStringLiteral("blue")] = curveToJson(m_curves.blue);
    return state;
}

void CurvesNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    ChannelCurves c;
    c.master = curveFromJson(state.value(QStringLiteral("master")));
    c.red = curveFromJson(state.value(QStringLiteral("red")));
    c.green = curveFromJson(state.value(QStringLiteral("green")));
    c.blue = curveFromJson(state.value(QStringLiteral("blue")));
    setCurves(c);
}
