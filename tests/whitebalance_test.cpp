// Unit test for the white-balance maths (WB v2): Planckian locus, neutralising
// multipliers, the linear-light WB matrix (identity at the as-shot reference),
// Kelvin estimation, and 3x3 matrix helpers.

#include "core/WhiteBalance.h"

#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool nearly(double a, double b, double tol = 1e-6)
{
    return std::abs(a - b) < tol;
}

static bool isIdentity(const double m[9], double tol = 1e-9)
{
    const double id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i)
        if (!nearly(m[i], id[i], tol))
            return false;
    return true;
}

int main()
{
    using namespace wb;

    // --- Planckian locus: 6500 K ≈ CIE D65 (x≈0.3127, y≈0.3290) ------------
    {
        double x = 0, y = 0;
        planckianXY(6500.0, x, y);
        CHECK(nearly(x, 0.3127, 0.01));
        CHECK(nearly(y, 0.3290, 0.01));
        // Warmer (lower K) is redder → larger x.
        double x2 = 0, y2 = 0;
        planckianXY(3000.0, x2, y2);
        CHECK(x2 > x);
    }

    // --- mat3 inverse round-trip on the sRGB matrix ------------------------
    {
        double inv[9], prod[9];
        CHECK(mat3Inverse(kSrgbXyzToRgb, inv));
        mat3Mul(kSrgbXyzToRgb, inv, prod);
        CHECK(isIdentity(prod, 1e-9));
    }

    // --- wbMatrix is identity at the as-shot reference (sRGB path) ----------
    {
        double W[9];
        wbMatrix(kIdentity3, kSrgbXyzToRgb, 6500.0, 6500.0, 0.0, W);
        CHECK(isIdentity(W, 1e-9));
    }

    // --- ... and with a synthetic non-identity camera matrix ---------------
    {
        const double camToRgb[9] = {1.6, -0.5, -0.1, -0.2, 1.4, -0.2, 0.0, -0.4, 1.4};
        const double xyzToCam[9] = {0.8, 0.1, 0.05, 0.2, 0.9, -0.1, 0.0, 0.1, 1.1};
        double W[9];
        wbMatrix(camToRgb, xyzToCam, 5200.0, 5200.0, 0.0, W);
        CHECK(isIdentity(W, 1e-9));
    }

    // --- Higher Kelvin warms the image: red gain up, blue gain down (sRGB) --
    {
        double W[9];
        wbMatrix(kIdentity3, kSrgbXyzToRgb, 6500.0, 8500.0, 0.0, W);
        CHECK(W[0] > 1.0); // diagonal red gain
        CHECK(W[8] < 1.0); // diagonal blue gain
        double Wc[9];
        wbMatrix(kIdentity3, kSrgbXyzToRgb, 6500.0, 4500.0, 0.0, Wc);
        CHECK(Wc[0] < 1.0); // cooler → red down
        CHECK(Wc[8] > 1.0); // blue up
    }

    // --- Tint scales green against R/B (sRGB path → diagonal matrix) -------
    {
        double Wm[9], Wg[9];
        wbMatrix(kIdentity3, kSrgbXyzToRgb, 6500.0, 6500.0, 60.0, Wm);  // magenta
        wbMatrix(kIdentity3, kSrgbXyzToRgb, 6500.0, 6500.0, -60.0, Wg); // green
        CHECK(Wm[4] < 1.0); // +tint (magenta) lowers green gain
        CHECK(Wg[4] > 1.0); // -tint (green) raises it
        CHECK(nearly(Wm[0], 1.0, 1e-9) && nearly(Wm[8], 1.0, 1e-9)); // R/B untouched
    }

    // --- estimateKelvin recovers the K used to synthesise as-shot mults -----
    {
        const double xyzToCam[9] = {0.8, 0.1, 0.05, 0.2, 0.9, -0.1, 0.0, 0.1, 1.1};
        double asShot[3];
        illuminantNeutralMul(xyzToCam, 5000.0, 0.0, asShot);
        const double K = estimateKelvin(xyzToCam, asShot);
        CHECK(nearly(K, 5000.0, 60.0));
    }

    std::puts("whitebalance_test: OK");
    return 0;
}
