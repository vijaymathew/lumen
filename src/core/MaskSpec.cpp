#include "core/MaskSpec.h"

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
    return m;
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
    return s;
}
