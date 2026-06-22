// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/CurvesNode.h"

#include <QJsonArray>

#include <array>

CurvesNode::CurvesNode()
    : EditNode(QStringLiteral("curves"))
{
}

void CurvesNode::setCurve(const Curve &curve)
{
    m_curve.setPoints(curve.points());
    invalidate();
}

void CurvesNode::contributeToPreviewLut(std::array<uint8_t, 256> &lut) const
{
    if (m_curve.isIdentity())
        return;
    const std::array<uint8_t, 256> mine = m_curve.buildLut();
    for (int i = 0; i < 256; ++i)
        lut[i] = mine[lut[i]]; // apply this curve after whatever is upstream
}

Image CurvesNode::apply(const Image &input) const
{
    if (input.isNull() || m_curve.isIdentity())
        return input;

    const std::array<uint8_t, 256> lut = m_curve.buildLut();

    // Build a 256x1, 4-band LUT image: RGB map through the curve, alpha is left
    // identity so transparency isn't altered.
    unsigned char data[256 * 4];
    for (int i = 0; i < 256; ++i) {
        data[i * 4 + 0] = lut[i];
        data[i * 4 + 1] = lut[i];
        data[i * 4 + 2] = lut[i];
        data[i * 4 + 3] = static_cast<unsigned char>(i); // alpha identity
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
    QJsonArray pts;
    for (const QPointF &p : m_curve.points()) {
        QJsonArray xy;
        xy.append(p.x());
        xy.append(p.y());
        pts.append(xy);
    }
    state[QStringLiteral("points")] = pts;
    return state;
}

void CurvesNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    const QJsonArray pts = state.value(QStringLiteral("points")).toArray();
    std::vector<QPointF> points;
    points.reserve(pts.size());
    for (const QJsonValue &v : pts) {
        const QJsonArray xy = v.toArray();
        if (xy.size() == 2)
            points.emplace_back(xy[0].toDouble(), xy[1].toDouble());
    }
    Curve c;
    if (points.size() >= 2)
        c.setPoints(std::move(points));
    setCurve(c);
}
