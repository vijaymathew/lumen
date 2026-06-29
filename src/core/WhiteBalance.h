#pragma once

// White-balance maths (Phase: WB v2). Pure, dependency-free helpers (no Qt/vips)
// so they are directly unit-testable. White balance is modelled as a Planckian
// (blackbody) illuminant at a colour temperature `kelvin` (plus a green/magenta
// `tint`), turned into a 3x3 matrix that is applied in LINEAR light.
//
// The matrix is the camera-accurate equivalent of redoing WB in the sensor's
// native space: out = C · diag(g) · C⁻¹ · in_linear, where C is the camera→linear
// -sRGB matrix and g the per-channel gain ratio between the target illuminant and
// the as-shot reference. For non-RAW images C is the identity and the sRGB
// XYZ→RGB matrix stands in for the "camera", so the matrix degrades to a plain
// diagonal linear-light gain. All matrices here are row-major 3x3.
namespace wb {

// CIE XYZ (D65) → linear sRGB, row-major. Used as the "camera" matrix for the
// non-RAW (sRGB) path.
extern const double kSrgbXyzToRgb[9];

// Identity matrix convenience (row-major).
extern const double kIdentity3[9];

// CIE 1931 xy chromaticity of a Planckian radiator at temperature `K`
// (Kim et al. 2002 approximation, valid ~1667–25000 K; K is clamped to range).
void planckianXY(double K, double &x, double &y);

// CIE XYZ (Y = 1) of the Planckian illuminant at `K`.
void xyzFromKelvin(double K, double xyz[3]);

// Per-channel neutralising multipliers for the illuminant at (`K`, `tint`),
// in the space defined by `xyzToCam` (XYZ → that space). Green-normalised so an
// overall scale is removed; `tint` then scales the green channel against R/B
// (positive = magenta = green gain down, negative = greener).
void illuminantNeutralMul(const double xyzToCam[9], double K, double tint, double mul[3]);

// Estimates the colour temperature whose neutralising multipliers best match
// `asShotMul` (camera as-shot multipliers), in the `xyzToCam` space. Used to seed
// the slider at the camera's as-shot point. Returns Kelvin.
double estimateKelvin(const double xyzToCam[9], const double asShotMul[3]);

// Builds the linear-light WB matrix `outW` (row-major 3x3) that retargets the
// image from the as-shot reference (`kAsShot`, tint 0) to (`kelvin`, `tint`):
//   W = camToRgb · diag( mul(kelvin,tint) / mul(kAsShot,0) ) · camToRgb⁻¹
// At kelvin == kAsShot && tint == 0 this is the identity. For the sRGB path pass
// camToRgb = kIdentity3 and xyzToCam = kSrgbXyzToRgb.
void wbMatrix(const double camToRgb[9], const double xyzToCam[9], double kAsShot,
              double kelvin, double tint, double outW[9]);

// Row-major 3x3 helpers.
void mat3Mul(const double a[9], const double b[9], double out[9]);
void mat3MulVec(const double m[9], const double v[3], double out[3]);
bool mat3Inverse(const double m[9], double out[9]); // false if singular

} // namespace wb
