#include "core/Curve.h"

#include <algorithm>
#include <cmath>

namespace {

// Fritsch–Carlson monotone tangents for the control points.
std::vector<double> monotoneTangents(const std::vector<QPointF> &p)
{
    const int n = static_cast<int>(p.size());
    std::vector<double> m(n, 0.0);
    if (n < 2)
        return m;

    std::vector<double> d(n - 1); // secant slopes
    for (int k = 0; k < n - 1; ++k)
        d[k] = (p[k + 1].y() - p[k].y()) / (p[k + 1].x() - p[k].x());

    m[0] = d[0];
    m[n - 1] = d[n - 2];
    for (int k = 1; k < n - 1; ++k)
        m[k] = (d[k - 1] + d[k]) * 0.5;

    for (int k = 0; k < n - 1; ++k) {
        if (d[k] == 0.0) {
            m[k] = 0.0;
            m[k + 1] = 0.0;
        } else {
            const double a = m[k] / d[k];
            const double b = m[k + 1] / d[k];
            const double h = a * a + b * b;
            if (h > 9.0) {
                const double t = 3.0 / std::sqrt(h);
                m[k] = t * a * d[k];
                m[k + 1] = t * b * d[k];
            }
        }
    }
    return m;
}

double evalAt(const std::vector<QPointF> &p, const std::vector<double> &m, double x)
{
    const int n = static_cast<int>(p.size());
    if (x <= p[0].x())
        return p[0].y();
    if (x >= p[n - 1].x())
        return p[n - 1].y();

    int k = 0;
    while (k < n - 1 && x > p[k + 1].x())
        ++k;

    const double h = p[k + 1].x() - p[k].x();
    const double t = (x - p[k].x()) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1;
    const double h10 = t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 = t3 - t2;
    const double y = h00 * p[k].y() + h10 * h * m[k]
                   + h01 * p[k + 1].y() + h11 * h * m[k + 1];
    return std::clamp(y, 0.0, 1.0);
}

} // namespace

Curve::Curve()
    : m_points{{0.0, 0.0}, {1.0, 1.0}}
{
}

void Curve::setPoints(std::vector<QPointF> points)
{
    for (QPointF &pt : points) {
        pt.setX(std::clamp(pt.x(), 0.0, 1.0));
        pt.setY(std::clamp(pt.y(), 0.0, 1.0));
    }
    std::sort(points.begin(), points.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });

    // Drop near-duplicate x (keeps points usable as spline knots).
    std::vector<QPointF> cleaned;
    for (const QPointF &pt : points) {
        if (!cleaned.empty() && pt.x() - cleaned.back().x() < 1e-4)
            continue;
        cleaned.push_back(pt);
    }

    if (cleaned.size() < 2)
        cleaned = {{0.0, 0.0}, {1.0, 1.0}};
    m_points = std::move(cleaned);
}

double Curve::evaluate(double x) const
{
    return evalAt(m_points, monotoneTangents(m_points), x);
}

std::array<uint8_t, 256> Curve::buildLut() const
{
    const std::vector<double> m = monotoneTangents(m_points);
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const double y = evalAt(m_points, m, i / 255.0);
        lut[i] = static_cast<uint8_t>(std::clamp(std::lround(y * 255.0), 0L, 255L));
    }
    return lut;
}

bool Curve::isIdentity() const
{
    return m_points.size() == 2 && m_points[0] == QPointF(0.0, 0.0)
        && m_points[1] == QPointF(1.0, 1.0);
}
