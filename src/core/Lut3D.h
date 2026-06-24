#pragma once

#include <QString>

#include <cstdint>
#include <vector>

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

    // Loads an Adobe/Resolve ".cube" 3D LUT (text: LUT_3D_SIZE n, then n^3 RGB
    // triples, red varying fastest). Float values are quantised to the 8-bit
    // cube; a non-default DOMAIN is not remapped (standard 0..1 looks only).
    // Returns an invalid Lut3D and sets *error on failure.
    static Lut3D fromCubeFile(const QString &path, QString *error = nullptr);

    // Trilinearly samples the cube. Inputs/outputs are in [0,1]; an invalid LUT
    // returns the input unchanged (identity).
    void sample(double r, double g, double b, double out[3]) const;

private:
    int m_dim = 0;
    std::vector<uint8_t> m_data; // dim^3 * 3, RGB, index ((b*dim+g)*dim+r)*3
};
