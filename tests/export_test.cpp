// Exercises the export path end to end: EditGraph.result() at full resolution →
// Image::saveToFile() → Image::fromFile() round-trip.

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/TuneNode.h"

#include <QDir>
#include <QFile>

#include <cstdio>
#include <memory>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    EditGraph graph;
    graph.setSource(Image::black(16, 12));
    auto *tune = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    tune->setExposure(1.0f);

    const Image result = graph.result();
    CHECK(!result.isNull());
    CHECK(result.width() == 16 && result.height() == 12);

    // The export worker composites on an independent EditGraph rebuilt from a
    // saveState() snapshot (so it never touches the live graph's node cache off
    // the UI thread). That rebuilt graph must produce pixel-identical output — the
    // whole point is the export matches what the user sees.
    {
        EditGraph clone;
        clone.setSource(Image::black(16, 12));
        clone.rebuildFromState(graph.saveState());
        const QImage a = graph.result().toQImage();
        const QImage b = clone.result().toQImage();
        CHECK(a.size() == b.size());
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x)
                CHECK(a.pixel(x, y) == b.pixel(x, y));
    }

    const QString out = QDir::temp().filePath(QStringLiteral("lumen_export_test.png"));
    QFile::remove(out);

    QString error;
    CHECK(result.saveToFile(out, &error));
    CHECK(QFile::exists(out));

    // Reload and confirm the dimensions round-trip.
    const Image reloaded = Image::fromFile(out, &error);
    CHECK(!reloaded.isNull());
    CHECK(reloaded.width() == 16 && reloaded.height() == 12);
    QFile::remove(out);

    // Each format (with quality on the lossy ones) writes a valid file that
    // reloads at the right size. This also checks the [Q=..] option suffix is
    // accepted by the lossy savers and ignored elsewhere.
    struct Case {
        const char *ext;
        int quality;
    };
    const Case cases[] = {
        {"png", -1}, {"jpg", 90}, {"jpg", 20}, {"webp", 80}, {"tiff", -1},
    };
    for (const Case &c : cases) {
        const QString p = QDir::temp().filePath(
            QStringLiteral("lumen_export_test.%1").arg(c.ext));
        QFile::remove(p);
        CHECK(result.saveToFile(p, c.quality, &error));
        CHECK(QFile::exists(p));
        const Image rr = Image::fromFile(p, &error);
        CHECK(!rr.isNull());
        CHECK(rr.width() == 16 && rr.height() == 12);
        QFile::remove(p);
    }

    // 16-bit export (lossless formats) writes a valid file that reloads.
    for (const char *ext : {"png", "tiff"}) {
        const QString p = QDir::temp().filePath(
            QStringLiteral("lumen_export16.%1").arg(ext));
        QFile::remove(p);
        CHECK(result.saveToFile(p, /*quality=*/-1, /*bits=*/16, &error));
        CHECK(QFile::exists(p));
        const Image rr = Image::fromFile(p, &error);
        CHECK(!rr.isNull());
        CHECK(rr.width() == 16 && rr.height() == 12);
        QFile::remove(p);
    }

    // Long-edge resize caps the longest side and preserves aspect (16×12 → 8×6).
    {
        const QString p = QDir::temp().filePath(QStringLiteral("lumen_resize.png"));
        QFile::remove(p);
        Image::ExportOptions opts;
        opts.longEdge = 8;
        CHECK(result.saveToFile(p, opts, &error));
        const Image rr = Image::fromFile(p, &error);
        CHECK(!rr.isNull());
        CHECK(rr.width() == 8 && rr.height() == 6);
        QFile::remove(p);

        // A long-edge larger than the image never upscales. (Distinct path: a
        // libvips load is cached on filename+mtime, so reusing `p` within the
        // same second could hand back the stale downscaled load above.)
        const QString p2 = QDir::temp().filePath(QStringLiteral("lumen_noupscale.png"));
        QFile::remove(p2);
        Image::ExportOptions big;
        big.longEdge = 100;
        CHECK(result.saveToFile(p2, big, &error));
        const Image rr2 = Image::fromFile(p2, &error);
        CHECK(!rr2.isNull());
        CHECK(rr2.width() == 16 && rr2.height() == 12);
        QFile::remove(p2);
    }

    // Wide-gamut export: convert a real RGB image into P3 / Adobe RGB. When
    // colour management is compiled in, the file carries an embedded ICC profile;
    // without it the option degrades to sRGB but must still write successfully.
    // (A 3-band RGB source mirrors the loaders' normalised working image — the
    // synthetic 1-band `black()` above has no colour to transform.)
    {
        unsigned char rgb[16 * 12 * 3];
        for (int i = 0; i < 16 * 12; ++i) {
            rgb[i * 3 + 0] = static_cast<unsigned char>(i % 256); // R ramp
            rgb[i * 3 + 1] = 128;                                 // G
            rgb[i * 3 + 2] = 64;                                  // B
        }
        const Image colour = Image::fromInterleaved(rgb, 16, 12, 3);
        CHECK(!colour.isNull());

        for (const Image::ColorSpace cs :
             {Image::ColorSpace::DisplayP3, Image::ColorSpace::AdobeRGB}) {
            const QString p = QDir::temp().filePath(
                QStringLiteral("lumen_gamut_%1.jpg").arg(static_cast<int>(cs)));
            QFile::remove(p);
            Image::ExportOptions opts;
            opts.quality = 92;
            opts.colorSpace = cs;
            CHECK(colour.saveToFile(p, opts, &error));
            const Image rr = Image::fromFile(p, &error);
            CHECK(!rr.isNull());
            CHECK(rr.width() == 16 && rr.height() == 12);
            QFile::remove(p);
        }
    }

    ImageBuffer::shutdownLibrary();
    std::puts("export_test: OK");
    return 0;
}
