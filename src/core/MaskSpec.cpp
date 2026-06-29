#include "core/MaskSpec.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace {

float smoothstep(float e0, float e1, float x)
{
    if (e0 == e1)
        return x < e0 ? 0.0f : 1.0f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

constexpr float kLumaR = 0.2126f;
constexpr float kLumaG = 0.7152f;
constexpr float kLumaB = 0.0722f;

// Soft coverage [0,1] of a single zone shape at normalised point (px,py).
// `feather` widens a soft band just inside each edge.
float shapeCoverage(const MaskZoneShape &s, float px, float py, float feather)
{
    const float f = std::clamp(feather, 0.0f, 1.0f);
    if (s.kind == MaskZoneShape::Polygon) {
        const int n = s.points.size();
        if (n < 3)
            return 0.0f;
        // Even-odd point-in-polygon, plus distance to the nearest edge for a
        // soft border (feather measured in normalised units).
        bool inside = false;
        float minDist = 1e9f;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const float xi = static_cast<float>(s.points[i].x());
            const float yi = static_cast<float>(s.points[i].y());
            const float xj = static_cast<float>(s.points[j].x());
            const float yj = static_cast<float>(s.points[j].y());
            if (((yi > py) != (yj > py)) &&
                (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12f) + xi))
                inside = !inside;
            // Distance from (px,py) to segment i-j.
            const float ex = xj - xi, ey = yj - yi;
            const float t = std::clamp(((px - xi) * ex + (py - yi) * ey) /
                                           (ex * ex + ey * ey + 1e-12f),
                                       0.0f, 1.0f);
            const float dx = px - (xi + t * ex), dy = py - (yi + t * ey);
            minDist = std::min(minDist, std::sqrt(dx * dx + dy * dy));
        }
        if (!inside)
            return 0.0f;
        return f > 1e-4f ? smoothstep(0.0f, f, minDist) : 1.0f;
    }

    // Rect / Ellipse share a rotated, centre-relative local frame.
    const float ca = std::cos(s.angle), sa = std::sin(s.angle);
    const float ox = px - static_cast<float>(s.center.x());
    const float oy = py - static_cast<float>(s.center.y());
    const float lx = ox * ca + oy * sa;
    const float ly = -ox * sa + oy * ca;
    const float rx = std::max(static_cast<float>(s.half.x()), 1e-4f);
    const float ry = std::max(static_cast<float>(s.half.y()), 1e-4f);

    if (s.kind == MaskZoneShape::Ellipse) {
        const float e = std::sqrt((lx / rx) * (lx / rx) + (ly / ry) * (ly / ry));
        return 1.0f - smoothstep(1.0f - f, 1.0f, e);
    }
    // Rect: soft band inside each edge (feather as a fraction of the half-extent).
    const float fxw = std::max(rx * f, 1e-5f);
    const float fyw = std::max(ry * f, 1e-5f);
    const float cx = 1.0f - smoothstep(rx - fxw, rx, std::abs(lx));
    const float cy = 1.0f - smoothstep(ry - fyw, ry, std::abs(ly));
    return cx * cy;
}

// Combined zone coverage: union of additive shapes minus union of subtractive
// ones. With no additive shapes, the included region is the whole image (so a
// lone subtractive shape simply cuts a hole).
float zoneCoverage(const MaskSpec &s, float px, float py)
{
    bool anyAdd = false;
    float inc = 0.0f, sub = 0.0f;
    for (const MaskZoneShape &shape : s.zones) {
        const float c = shapeCoverage(shape, px, py, s.zoneFeather);
        if (shape.subtract)
            sub = std::max(sub, c);
        else {
            anyAdd = true;
            inc = std::max(inc, c);
        }
    }
    if (!anyAdd)
        inc = 1.0f;
    return std::clamp(inc - sub, 0.0f, 1.0f);
}

} // namespace

MaskBuffer evaluateMask(const MaskSpec &s, int w, int h, const uint8_t *rgba, int bands)
{
    MaskBuffer m;
    m.width = w;
    m.height = h;
    if (w <= 0 || h <= 0)
        return m;
    m.data.assign(static_cast<size_t>(w) * h, 0.0f);

    switch (s.type) {
    case MaskSpec::None:
        std::fill(m.data.begin(), m.data.end(), 1.0f);
        break;

    case MaskSpec::Brush:
        m = upscaleMask(s.brush, w, h);
        break;

    case MaskSpec::LinearGradient: {
        const float ax = static_cast<float>(s.gradTo.x() - s.gradFrom.x());
        const float ay = static_cast<float>(s.gradTo.y() - s.gradFrom.y());
        const float len2 = std::max(ax * ax + ay * ay, 1e-9f);
        const float fx = static_cast<float>(s.gradFrom.x());
        const float fy = static_cast<float>(s.gradFrom.y());
        for (int y = 0; y < h; ++y) {
            const float py = (y + 0.5f) / h;
            for (int x = 0; x < w; ++x) {
                const float px = (x + 0.5f) / w;
                // Projection parameter along from→to, 0 at `from`, 1 at `to`.
                const float t = ((px - fx) * ax + (py - fy) * ay) / len2;
                m.data[static_cast<size_t>(y) * w + x] = std::clamp(t, 0.0f, 1.0f);
            }
        }
        break;
    }

    case MaskSpec::Radial: {
        const float ca = std::cos(s.angle);
        const float sa = std::sin(s.angle);
        const float rx = std::max(s.radiusX, 1e-4f);
        const float ry = std::max(s.radiusY, 1e-4f);
        const float feather = std::clamp(s.feather, 0.0f, 1.0f);
        const float cx = static_cast<float>(s.center.x());
        const float cy = static_cast<float>(s.center.y());
        for (int y = 0; y < h; ++y) {
            const float py = (y + 0.5f) / h - cy;
            for (int x = 0; x < w; ++x) {
                const float px = (x + 0.5f) / w - cx;
                // Express in the ellipse's local (un-rotated) frame.
                const float lx = px * ca + py * sa;
                const float ly = -px * sa + py * ca;
                const float e = std::sqrt((lx / rx) * (lx / rx) + (ly / ry) * (ly / ry));
                const float inside = 1.0f - smoothstep(1.0f - feather, 1.0f, e);
                m.data[static_cast<size_t>(y) * w + x] = s.radialInside ? inside : 1.0f - inside;
            }
        }
        break;
    }

    case MaskSpec::Luminosity: {
        if (!rgba || bands < 3)
            break; // leaves coverage at 0
        const float f = std::max(s.feather, 0.001f);
        for (long long i = 0; i < static_cast<long long>(w) * h; ++i) {
            const uint8_t *p = rgba + i * bands;
            const float L = (kLumaR * p[0] + kLumaG * p[1] + kLumaB * p[2]) / 255.0f;
            m.data[i] = smoothstep(s.low - f, s.low, L)
                      * (1.0f - smoothstep(s.high, s.high + f, L));
        }
        break;
    }

    case MaskSpec::Colour:
        if (rgba && bands >= 3)
            m = colorAffinityMask(rgba, w, h, bands, s.targetR, s.targetG, s.targetB,
                                  s.colorRange);
        break;
    }

    if (s.invert)
        for (float &v : m.data)
            v = 1.0f - v;

    // Exclusive zone: gate coverage to the drawn shapes (whole image if empty).
    if (!s.zones.empty()) {
        for (int y = 0; y < h; ++y) {
            const float py = (y + 0.5f) / h;
            for (int x = 0; x < w; ++x) {
                const float px = (x + 0.5f) / w;
                m.data[static_cast<size_t>(y) * w + x] *= zoneCoverage(s, px, py);
            }
        }
    }
    return m;
}

QJsonObject MaskZoneShape::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("kind")] = static_cast<int>(kind);
    o[QStringLiteral("subtract")] = subtract;
    o[QStringLiteral("cx")] = center.x();
    o[QStringLiteral("cy")] = center.y();
    o[QStringLiteral("hx")] = half.x();
    o[QStringLiteral("hy")] = half.y();
    o[QStringLiteral("angle")] = angle;
    if (!points.isEmpty()) {
        QJsonArray pts;
        for (const QPointF &p : points) {
            pts.append(p.x());
            pts.append(p.y());
        }
        o[QStringLiteral("points")] = pts;
    }
    return o;
}

MaskZoneShape MaskZoneShape::fromJson(const QJsonObject &o)
{
    MaskZoneShape s;
    s.kind = static_cast<Kind>(o.value(QStringLiteral("kind")).toInt(0));
    s.subtract = o.value(QStringLiteral("subtract")).toBool(false);
    s.center = {o.value(QStringLiteral("cx")).toDouble(0.5),
                o.value(QStringLiteral("cy")).toDouble(0.5)};
    s.half = {o.value(QStringLiteral("hx")).toDouble(0.2),
              o.value(QStringLiteral("hy")).toDouble(0.2)};
    s.angle = static_cast<float>(o.value(QStringLiteral("angle")).toDouble(0.0));
    const QJsonArray pts = o.value(QStringLiteral("points")).toArray();
    for (int i = 0; i + 1 < pts.size(); i += 2)
        s.points.append(QPointF(pts[i].toDouble(), pts[i + 1].toDouble()));
    return s;
}

QJsonObject MaskSpec::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = static_cast<int>(type);
    o[QStringLiteral("invert")] = invert;
    o[QStringLiteral("feather")] = feather;
    o[QStringLiteral("gradFromX")] = gradFrom.x();
    o[QStringLiteral("gradFromY")] = gradFrom.y();
    o[QStringLiteral("gradToX")] = gradTo.x();
    o[QStringLiteral("gradToY")] = gradTo.y();
    o[QStringLiteral("centerX")] = center.x();
    o[QStringLiteral("centerY")] = center.y();
    o[QStringLiteral("radiusX")] = radiusX;
    o[QStringLiteral("radiusY")] = radiusY;
    o[QStringLiteral("angle")] = angle;
    o[QStringLiteral("radialInside")] = radialInside;
    o[QStringLiteral("low")] = low;
    o[QStringLiteral("high")] = high;
    o[QStringLiteral("targetR")] = targetR;
    o[QStringLiteral("targetG")] = targetG;
    o[QStringLiteral("targetB")] = targetB;
    o[QStringLiteral("colorRange")] = colorRange;
    if (!brush.isEmpty())
        o[QStringLiteral("brush")] = encodeMaskPng(brush);
    if (!zones.empty()) {
        QJsonArray z;
        for (const MaskZoneShape &shape : zones)
            z.append(shape.toJson());
        o[QStringLiteral("zones")] = z;
        o[QStringLiteral("zoneFeather")] = zoneFeather;
    }
    return o;
}

MaskSpec MaskSpec::fromJson(const QJsonObject &o)
{
    MaskSpec s;
    s.type = static_cast<Type>(o.value(QStringLiteral("type")).toInt(0));
    s.invert = o.value(QStringLiteral("invert")).toBool(false);
    s.feather = static_cast<float>(o.value(QStringLiteral("feather")).toDouble(0.1));
    s.gradFrom = {o.value(QStringLiteral("gradFromX")).toDouble(0.0),
                  o.value(QStringLiteral("gradFromY")).toDouble(0.5)};
    s.gradTo = {o.value(QStringLiteral("gradToX")).toDouble(1.0),
                o.value(QStringLiteral("gradToY")).toDouble(0.5)};
    s.center = {o.value(QStringLiteral("centerX")).toDouble(0.5),
                o.value(QStringLiteral("centerY")).toDouble(0.5)};
    s.radiusX = static_cast<float>(o.value(QStringLiteral("radiusX")).toDouble(0.3));
    s.radiusY = static_cast<float>(o.value(QStringLiteral("radiusY")).toDouble(0.3));
    s.angle = static_cast<float>(o.value(QStringLiteral("angle")).toDouble(0.0));
    s.radialInside = o.value(QStringLiteral("radialInside")).toBool(true);
    s.low = static_cast<float>(o.value(QStringLiteral("low")).toDouble(0.0));
    s.high = static_cast<float>(o.value(QStringLiteral("high")).toDouble(1.0));
    s.targetR = static_cast<float>(o.value(QStringLiteral("targetR")).toDouble(0.0));
    s.targetG = static_cast<float>(o.value(QStringLiteral("targetG")).toDouble(0.0));
    s.targetB = static_cast<float>(o.value(QStringLiteral("targetB")).toDouble(0.0));
    s.colorRange = static_cast<float>(o.value(QStringLiteral("colorRange")).toDouble(0.3));
    s.brush = decodeMaskPng(o.value(QStringLiteral("brush")).toString());
    const QJsonArray z = o.value(QStringLiteral("zones")).toArray();
    for (const QJsonValue &v : z)
        s.zones.push_back(MaskZoneShape::fromJson(v.toObject()));
    s.zoneFeather = static_cast<float>(o.value(QStringLiteral("zoneFeather")).toDouble(0.05));
    return s;
}
