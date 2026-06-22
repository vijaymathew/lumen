// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/CurvesNode.h"

#include <QJsonArray>

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

    const std::array<uint8_t, 256> r = effectiveLut(0);
    const std::array<uint8_t, 256> g = effectiveLut(1);
    const std::array<uint8_t, 256> b = effectiveLut(2);

    // 256x1, 4-band LUT: R/G/B map through their effective curves, alpha identity.
    unsigned char data[256 * 4];
    for (int i = 0; i < 256; ++i) {
        data[i * 4 + 0] = r[i];
        data[i * 4 + 1] = g[i];
        data[i * 4 + 2] = b[i];
        data[i * 4 + 3] = static_cast<unsigned char>(i);
    }
    VipsImage *lutImg = vips_image_new_from_memory_copy(data, sizeof(data), 256, 1, 4,
                                                        VIPS_FORMAT_UCHAR);
    if (!lutImg)
        return input;

    VipsImage *out = nullptr;
    const int rc = vips_maplut(input.handle(), &out, lutImg, nullptr);
    g_object_unref(lutImg);
    if (rc)
        return input;

    return Image::adopt(out);
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
