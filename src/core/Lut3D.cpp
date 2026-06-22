#include "core/Lut3D.h"

#include "core/Image.h"

#include <QImage>

#include <algorithm>
#include <cmath>

Lut3D Lut3D::fromHaldFile(const QString &path, QString *error)
{
    QString loadError;
    Image img = Image::fromFile(path, &loadError);
    if (img.isNull()) {
        if (error)
            *error = loadError;
        return {};
    }
    return fromHaldImage(img.toQImage(), error);
}

Lut3D Lut3D::fromHaldImage(const QImage &source, QString *error)
{
    const QImage img = source.convertToFormat(QImage::Format_RGBA8888);
    const int side = img.width();
    if (side <= 0 || img.height() != side) {
        if (error)
            *error = QStringLiteral("HALD CLUT must be a square image");
        return {};
    }

    // The cube edge `dim` satisfies dim^3 == side^2 (one cube entry per pixel).
    const long long pixels = static_cast<long long>(side) * side;
    int dim = static_cast<int>(std::lround(std::cbrt(static_cast<double>(pixels))));
    while (static_cast<long long>(dim) * dim * dim < pixels)
        ++dim;
    while (dim > 0 && static_cast<long long>(dim) * dim * dim > pixels)
        --dim;
    if (dim < 2 || static_cast<long long>(dim) * dim * dim != pixels) {
        if (error)
            *error = QStringLiteral("Not a valid HALD CLUT (side %1 is not n^3)")
                         .arg(side);
        return {};
    }

    Lut3D lut;
    lut.m_dim = dim;
    lut.m_data.resize(static_cast<size_t>(pixels) * 3);
    for (int y = 0; y < side; ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < side; ++x) {
            const size_t p = static_cast<size_t>(y) * side + x;
            lut.m_data[p * 3 + 0] = line[x * 4 + 0];
            lut.m_data[p * 3 + 1] = line[x * 4 + 1];
            lut.m_data[p * 3 + 2] = line[x * 4 + 2];
        }
    }
    return lut;
}

void Lut3D::sample(double r, double g, double b, double out[3]) const
{
    if (m_dim < 2) {
        out[0] = r;
        out[1] = g;
        out[2] = b;
        return;
    }

    const int d = m_dim;
    const double fr = std::clamp(r, 0.0, 1.0) * (d - 1);
    const double fg = std::clamp(g, 0.0, 1.0) * (d - 1);
    const double fb = std::clamp(b, 0.0, 1.0) * (d - 1);

    const int r0 = std::clamp(static_cast<int>(std::floor(fr)), 0, d - 1);
    const int g0 = std::clamp(static_cast<int>(std::floor(fg)), 0, d - 1);
    const int b0 = std::clamp(static_cast<int>(std::floor(fb)), 0, d - 1);
    const int r1 = std::min(r0 + 1, d - 1);
    const int g1 = std::min(g0 + 1, d - 1);
    const int b1 = std::min(b0 + 1, d - 1);
    const double dr = fr - r0;
    const double dg = fg - g0;
    const double db = fb - b0;

    for (int ch = 0; ch < 3; ++ch) {
        const auto V = [&](int ri, int gi, int bi) {
            const size_t idx =
                ((static_cast<size_t>(bi) * d + gi) * d + ri) * 3 + ch;
            return m_data[idx] / 255.0;
        };
        const double c00 = V(r0, g0, b0) * (1 - dr) + V(r1, g0, b0) * dr;
        const double c10 = V(r0, g1, b0) * (1 - dr) + V(r1, g1, b0) * dr;
        const double c01 = V(r0, g0, b1) * (1 - dr) + V(r1, g0, b1) * dr;
        const double c11 = V(r0, g1, b1) * (1 - dr) + V(r1, g1, b1) * dr;
        const double c0 = c00 * (1 - dg) + c10 * dg;
        const double c1 = c01 * (1 - dg) + c11 * dg;
        out[ch] = c0 * (1 - db) + c1 * db;
    }
}
