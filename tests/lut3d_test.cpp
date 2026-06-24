// Unit test for the HALD CLUT loader (Lut3D): parsing dimensions, identity
// round-trip via trilinear sampling, file load, and invalid-input handling.

#include "core/ImageBuffer.h"
#include "core/Lut3D.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>

#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// Builds an identity HALD CLUT of the given cube edge (dim must be a perfect
// square so side = dim^1.5 is integer; e.g. dim=4 -> side=8).
static QImage makeIdentityHald(int dim)
{
    const int side = static_cast<int>(std::lround(std::sqrt(double(dim) * dim * dim)));
    QImage img(side, side, QImage::Format_RGBA8888);
    for (int p = 0; p < side * side; ++p) {
        const int r = p % dim;
        const int g = (p / dim) % dim;
        const int b = p / (dim * dim);
        const auto v = [dim](int i) {
            return static_cast<int>(std::lround(i / double(dim - 1) * 255.0));
        };
        img.setPixelColor(p % side, p / side, QColor(v(r), v(g), v(b)));
    }
    return img;
}

static bool nearly(double a, double b)
{
    return std::abs(a - b) < 0.01;
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Identity CLUT round-trips through trilinear sampling.
    const QImage hald = makeIdentityHald(4); // 8x8, cube edge 4
    Lut3D lut = Lut3D::fromHaldImage(hald);
    CHECK(lut.isValid());
    CHECK(lut.dim() == 4);

    double out[3];
    lut.sample(0.2, 0.5, 0.8, out);
    CHECK(nearly(out[0], 0.2) && nearly(out[1], 0.5) && nearly(out[2], 0.8));
    lut.sample(0.0, 0.0, 0.0, out);
    CHECK(nearly(out[0], 0.0) && nearly(out[1], 0.0) && nearly(out[2], 0.0));
    lut.sample(1.0, 1.0, 1.0, out);
    CHECK(nearly(out[0], 1.0) && nearly(out[1], 1.0) && nearly(out[2], 1.0));

    // Load the same CLUT from a file.
    const QString path = QDir::temp().filePath(QStringLiteral("lumen_hald_test.png"));
    QFile::remove(path);
    CHECK(hald.save(path, "PNG"));
    QString error;
    Lut3D fromFile = Lut3D::fromHaldFile(path, &error);
    CHECK(fromFile.isValid());
    CHECK(fromFile.dim() == 4);
    fromFile.sample(0.4, 0.6, 0.1, out);
    CHECK(nearly(out[0], 0.4) && nearly(out[1], 0.6) && nearly(out[2], 0.1));
    QFile::remove(path);

    // Invalid inputs.
    Lut3D bad1 = Lut3D::fromHaldImage(QImage(8, 4, QImage::Format_RGBA8888), &error);
    CHECK(!bad1.isValid()); // not square
    Lut3D bad2 = Lut3D::fromHaldImage(QImage(10, 10, QImage::Format_RGBA8888), &error);
    CHECK(!bad2.isValid()); // side^2 not a perfect cube

    // --- .cube loader ------------------------------------------------------
    {
        // A 2^3 identity cube (red varies fastest), with comments + a TITLE +
        // DOMAIN lines that must be tolerated.
        const QString cubePath = QDir::temp().filePath(QStringLiteral("lumen_test.cube"));
        QFile::remove(cubePath);
        QFile f(cubePath);
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("# a test cube\nTITLE \"Identity\"\nLUT_3D_SIZE 2\n"
                "DOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n");
        for (int b = 0; b < 2; ++b)
            for (int g = 0; g < 2; ++g)
                for (int r = 0; r < 2; ++r) // red fastest
                    f.write(QStringLiteral("%1 %2 %3\n").arg(r).arg(g).arg(b).toUtf8());
        f.close();

        Lut3D cube = Lut3D::fromCubeFile(cubePath, &error);
        CHECK(cube.isValid());
        CHECK(cube.dim() == 2);
        cube.sample(0.0, 0.0, 0.0, out);
        CHECK(nearly(out[0], 0.0) && nearly(out[1], 0.0) && nearly(out[2], 0.0));
        cube.sample(1.0, 1.0, 1.0, out);
        CHECK(nearly(out[0], 1.0) && nearly(out[1], 1.0) && nearly(out[2], 1.0));
        cube.sample(0.3, 0.6, 0.9, out); // identity → trilinear returns the input
        CHECK(nearly(out[0], 0.3) && nearly(out[1], 0.6) && nearly(out[2], 0.9));
        QFile::remove(cubePath);

        // Missing LUT_3D_SIZE → invalid.
        const QString badPath = QDir::temp().filePath(QStringLiteral("lumen_bad.cube"));
        QFile bf(badPath);
        CHECK(bf.open(QIODevice::WriteOnly | QIODevice::Text));
        bf.write("0 0 0\n1 1 1\n");
        bf.close();
        Lut3D badCube = Lut3D::fromCubeFile(badPath, &error);
        CHECK(!badCube.isValid());
        QFile::remove(badPath);
    }

    // An invalid LUT samples as identity.
    Lut3D empty;
    empty.sample(0.3, 0.7, 0.9, out);
    CHECK(nearly(out[0], 0.3) && nearly(out[1], 0.7) && nearly(out[2], 0.9));

    ImageBuffer::shutdownLibrary();
    std::puts("lut3d_test: OK");
    return 0;
}
