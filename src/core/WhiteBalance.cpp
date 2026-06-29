#include "core/WhiteBalance.h"

#include <algorithm>
#include <cmath>

namespace wb {

const double kSrgbXyzToRgb[9] = {
    3.2404542, -1.5371385, -0.4985314,
    -0.9692660, 1.8760108, 0.0415560,
    0.0556434, -0.2040259, 1.0572252,
};

const double kIdentity3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

void mat3MulVec(const double m[9], const double v[3], double out[3])
{
    for (int r = 0; r < 3; ++r)
        out[r] = m[r * 3 + 0] * v[0] + m[r * 3 + 1] * v[1] + m[r * 3 + 2] * v[2];
}

void mat3Mul(const double a[9], const double b[9], double out[9])
{
    double tmp[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            tmp[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c]
                           + a[r * 3 + 1] * b[1 * 3 + c]
                           + a[r * 3 + 2] * b[2 * 3 + c];
    std::copy(tmp, tmp + 9, out);
}

bool mat3Inverse(const double m[9], double out[9])
{
    const double a = m[0], b = m[1], c = m[2];
    const double d = m[3], e = m[4], f = m[5];
    const double g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h;
    const double B = -(d * i - f * g);
    const double C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    if (std::abs(det) < 1e-12)
        return false;
    const double inv = 1.0 / det;
    out[0] = A * inv;
    out[1] = -(b * i - c * h) * inv;
    out[2] = (b * f - c * e) * inv;
    out[3] = B * inv;
    out[4] = (a * i - c * g) * inv;
    out[5] = -(a * f - c * d) * inv;
    out[6] = C * inv;
    out[7] = -(a * h - b * g) * inv;
    out[8] = (a * e - b * d) * inv;
    return true;
}

void planckianXY(double K, double &x, double &y)
{
    K = std::clamp(K, 1667.0, 25000.0);
    const double t = 1.0 / K;
    const double t2 = t * t;
    const double t3 = t2 * t;
    if (K <= 4000.0)
        x = -0.2661239e9 * t3 - 0.2343589e6 * t2 + 0.8776956e3 * t + 0.179910;
    else
        x = -3.0258469e9 * t3 + 2.1070379e6 * t2 + 0.2226347e3 * t + 0.240390;

    const double x2 = x * x;
    const double x3 = x2 * x;
    if (K <= 2222.0)
        y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    else if (K <= 4000.0)
        y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    else
        y = 3.0817580 * x3 - 5.8733867 * x2 + 3.75112997 * x - 0.37001483;
}

void xyzFromKelvin(double K, double xyz[3])
{
    double x = 0.0, y = 0.0;
    planckianXY(K, x, y);
    xyz[0] = x / y;
    xyz[1] = 1.0;
    xyz[2] = (1.0 - x - y) / y;
}

void illuminantNeutralMul(const double xyzToCam[9], double K, double tint, double mul[3])
{
    double xyz[3];
    xyzFromKelvin(K, xyz);
    double cam[3];
    mat3MulVec(xyzToCam, xyz, cam);
    for (int c = 0; c < 3; ++c)
        mul[c] = 1.0 / (std::abs(cam[c]) < 1e-9 ? 1e-9 : cam[c]);
    // Green-normalise so an overall scale (exposure) is removed.
    const double g = mul[1];
    for (int c = 0; c < 3; ++c)
        mul[c] /= g;
    // Tint scales green against R/B: +tint (magenta) lowers the green gain,
    // -tint (green) raises it. Clamped to stay positive and sane.
    const double factor = std::clamp(1.0 - 0.0015 * tint, 0.2, 5.0);
    mul[1] *= factor;
}

double estimateKelvin(const double xyzToCam[9], const double asShotMul[3])
{
    // Compare in green-normalised (r,b) chroma space against the as-shot mults.
    double ref[3] = {asShotMul[0], asShotMul[1], asShotMul[2]};
    if (std::abs(ref[1]) < 1e-9)
        return 6500.0;
    const double gn = ref[1];
    for (int c = 0; c < 3; ++c)
        ref[c] /= gn;

    auto cost = [&](double K) {
        double m[3];
        illuminantNeutralMul(xyzToCam, K, 0.0, m);
        const double dr = m[0] - ref[0];
        const double db = m[2] - ref[2];
        return dr * dr + db * db;
    };

    // Coarse scan then local refine over the usable range.
    double best = 6500.0, bestCost = cost(best);
    for (double K = 2000.0; K <= 15000.0; K += 50.0) {
        const double c = cost(K);
        if (c < bestCost) {
            bestCost = c;
            best = K;
        }
    }
    for (double step = 25.0; step >= 1.0; step *= 0.5) {
        for (int s = -1; s <= 1; s += 2) {
            const double K = best + s * step;
            if (K < 1667.0 || K > 25000.0)
                continue;
            const double c = cost(K);
            if (c < bestCost) {
                bestCost = c;
                best = K;
            }
        }
    }
    return best;
}

void wbMatrix(const double camToRgb[9], const double xyzToCam[9], double kAsShot,
              double kelvin, double tint, double outW[9])
{
    double mulT[3], mulR[3];
    illuminantNeutralMul(xyzToCam, kelvin, tint, mulT);
    illuminantNeutralMul(xyzToCam, kAsShot, 0.0, mulR);

    double g[3];
    for (int c = 0; c < 3; ++c)
        g[c] = mulT[c] / mulR[c];

    // W = camToRgb · diag(g) · camToRgb⁻¹
    double camInv[9];
    if (!mat3Inverse(camToRgb, camInv)) {
        // Degenerate camera matrix: fall back to a plain diagonal gain.
        for (int i = 0; i < 9; ++i)
            outW[i] = 0.0;
        outW[0] = g[0];
        outW[4] = g[1];
        outW[8] = g[2];
        return;
    }
    double diagInv[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            diagInv[r * 3 + c] = g[r] * camInv[r * 3 + c]; // diag(g) · camInv
    mat3Mul(camToRgb, diagInv, outW);
}

} // namespace wb
