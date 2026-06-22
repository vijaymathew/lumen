#pragma once

#include <QPointF>

#include <array>
#include <cstdint>
#include <vector>

// Curve is a tone curve defined by control points in the unit square [0,1]^2,
// interpolated with a monotone cubic spline (no overshoot — important for tone
// curves). It produces a 256-entry 8-bit LUT used by both the GPU preview
// (texture) and the libvips export (vips_maplut), so they match exactly.
class Curve {
public:
    // Identity: (0,0) -> (1,1).
    Curve();

    const std::vector<QPointF> &points() const { return m_points; }

    // Replaces the control points. Values are clamped to [0,1], sorted by x,
    // near-duplicate x are dropped, and at least two points are guaranteed.
    void setPoints(std::vector<QPointF> points);

    // y for a given x in [0,1] (clamped to [0,1]).
    double evaluate(double x) const;

    // lut[i] = round(255 * evaluate(i/255)).
    std::array<uint8_t, 256> buildLut() const;

    bool isIdentity() const;

private:
    std::vector<QPointF> m_points;
};
