#pragma once

#include <QString>

#include <cstdint>
#include <vector>

class QByteArray;
class QImage;

// Lut3D is a 3D colour lookup table (a cube of edge `dim` samples per channel),
// loaded from a HALD CLUT image. A HALD CLUT of level n is a square image of
// side n^3 encoding a cube of edge n^2: pixel p = y*side + x holds the output
// colour for cube coordinate (r = p % dim, g = (p/dim) % dim, b = p/dim^2).
//
// This is the loader (Phase 4.1). Applying it to a whole image (LutNode, GPU 3D
// texture + libvips) and a look-intensity blend come in 4.2/4.3. The trilinear
// sample() here makes the loader directly testable.
class Lut3D {
public:
    Lut3D() = default;

    bool isValid() const { return m_dim >= 2; }
    int dim() const { return m_dim; } // cube edge (samples per channel)

    // Loads a HALD CLUT image file. Returns an invalid Lut3D and sets *error on
    // failure (unreadable, non-square, or side not a perfect cube).
    static Lut3D fromHaldFile(const QString &path, QString *error = nullptr);

    // Builds from an already-decoded HALD CLUT image.
    static Lut3D fromHaldImage(const QImage &image, QString *error = nullptr);

    // Builds a HALD CLUT from raw encoded-image bytes (PNG/JPEG/…). Lets a LUT be
    // embedded in a project and rebuilt without the original file on disk.
    static Lut3D fromHaldData(const QByteArray &bytes, QString *error = nullptr);

    // Loads an Adobe/Resolve ".cube" 3D LUT (text: LUT_3D_SIZE n, then n^3 RGB
    // triples, red varying fastest). Values are stored at full float precision
    // (no 8-bit quantisation), output values are not clamped (HDR looks survive),
    // and a non-default DOMAIN_MIN/DOMAIN_MAX is honoured (inputs are remapped
    // from the LUT's domain to [0,1] at sample time).
    // Returns an invalid Lut3D and sets *error on failure.
    static Lut3D fromCubeFile(const QString &path, QString *error = nullptr);

    // As fromCubeFile, but parses ".cube" text already held in memory (used to
    // rebuild a LUT embedded in a project).
    static Lut3D fromCubeData(const QByteArray &bytes, QString *error = nullptr);

    // Trilinearly samples the cube. Inputs are mapped from the LUT domain
    // (default [0,1]) onto the cube; outputs are unclamped. An invalid LUT
    // returns the input unchanged (identity).
    void sample(double r, double g, double b, double out[3]) const;

private:
    int m_dim = 0;
    std::vector<float> m_data; // dim^3 * 3, RGB, index ((b*dim+g)*dim+r)*3
    // Input domain (one min/max per channel); inputs are remapped to [0,1]
    // before indexing. Defaults to the standard 0..1 domain.
    double m_domainMin[3] = {0.0, 0.0, 0.0};
    double m_domainMax[3] = {1.0, 1.0, 1.0};
};
