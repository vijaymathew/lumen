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

    ImageBuffer::shutdownLibrary();
    std::puts("export_test: OK");
    return 0;
}
