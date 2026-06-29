#include "core/Lut3D.h"

#include "core/Image.h"

#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>

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
            lut.m_data[p * 3 + 0] = line[x * 4 + 0] / 255.0f;
            lut.m_data[p * 3 + 1] = line[x * 4 + 1] / 255.0f;
            lut.m_data[p * 3 + 2] = line[x * 4 + 2] / 255.0f;
        }
    }
    return lut;
}

Lut3D Lut3D::fromCubeFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Could not open '%1'").arg(path);
        return {};
    }

    static const QRegularExpression ws(QStringLiteral("\\s+"));
    int dim = 0;
    std::vector<float> values; // RGB triples in file order (red varies fastest)
    double domainMin[3] = {0.0, 0.0, 0.0};
    double domainMax[3] = {1.0, 1.0, 1.0};
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList tok = line.split(ws, Qt::SkipEmptyParts);
        if (tok.isEmpty())
            continue;
        const QString key = tok.first().toUpper();
        if (key == QLatin1String("LUT_3D_SIZE")) {
            dim = tok.value(1).toInt();
        } else if (key == QLatin1String("LUT_1D_SIZE")) {
            if (error)
                *error = QStringLiteral("1D .cube LUTs are not supported");
            return {};
        } else if (key == QLatin1String("DOMAIN_MIN")
                   || key == QLatin1String("DOMAIN_MAX")
                   || key == QLatin1String("LUT_3D_INPUT_RANGE")) {
            // LUT_3D_INPUT_RANGE gives a single min/max applied to all channels;
            // DOMAIN_MIN/MAX give per-channel triples.
            double *dst = (key == QLatin1String("DOMAIN_MIN")) ? domainMin : domainMax;
            if (key == QLatin1String("LUT_3D_INPUT_RANGE")) {
                bool lo = false, hi = false;
                const double a = tok.value(1).toDouble(&lo);
                const double b = tok.value(2).toDouble(&hi);
                if (lo && hi)
                    for (int c = 0; c < 3; ++c) {
                        domainMin[c] = a;
                        domainMax[c] = b;
                    }
            } else {
                for (int c = 0; c < 3; ++c) {
                    bool ok = false;
                    const double v = tok.value(c + 1).toDouble(&ok);
                    if (ok)
                        dst[c] = v;
                }
            }
        } else if (key == QLatin1String("TITLE")) {
            continue; // metadata
        } else if (tok.size() == 3) {
            bool ok0 = false, ok1 = false, ok2 = false;
            const float r = tok[0].toFloat(&ok0);
            const float g = tok[1].toFloat(&ok1);
            const float b = tok[2].toFloat(&ok2);
            if (ok0 && ok1 && ok2) {
                values.push_back(r);
                values.push_back(g);
                values.push_back(b);
            }
        }
        // anything else is ignored
    }

    if (dim < 2) {
        if (error)
            *error = QStringLiteral("Not a valid .cube (missing LUT_3D_SIZE)");
        return {};
    }
    const long long entries = static_cast<long long>(dim) * dim * dim;
    if (static_cast<long long>(values.size()) != entries * 3) {
        if (error)
            *error = QStringLiteral("Cube has %1 entries, expected %2 (%3^3)")
                         .arg(values.size() / 3)
                         .arg(entries)
                         .arg(dim);
        return {};
    }

    // The .cube ordering (red fastest) matches the internal index
    // ((b*dim+g)*dim+r), so the triples fill sequentially. Stored at full float
    // precision and unclamped (HDR / out-of-range outputs survive).
    Lut3D lut;
    lut.m_dim = dim;
    lut.m_data = std::move(values);
    for (int c = 0; c < 3; ++c) {
        lut.m_domainMin[c] = domainMin[c];
        lut.m_domainMax[c] = domainMax[c];
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
    // Map each input from the LUT's domain onto [0,1] before indexing the cube.
    const auto norm = [](double v, double lo, double hi) {
        const double span = hi - lo;
        return std::clamp(span != 0.0 ? (v - lo) / span : 0.0, 0.0, 1.0);
    };
    const double fr = norm(r, m_domainMin[0], m_domainMax[0]) * (d - 1);
    const double fg = norm(g, m_domainMin[1], m_domainMax[1]) * (d - 1);
    const double fb = norm(b, m_domainMin[2], m_domainMax[2]) * (d - 1);

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
            return static_cast<double>(m_data[idx]);
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
