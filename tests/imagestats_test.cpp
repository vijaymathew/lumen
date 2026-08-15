// Unit test for imagestats::computeFolderStats. No RAW fixtures ship with the
// repo (see raw_test.cpp), so this exercises the raster path: colour/mono
// classification, recursion into subfolders, and ignoring non-image files.

#include "core/ImageStats.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace {
void writeSolid(const QString &path, const QColor &c)
{
    QImage img(16, 16, QImage::Format_RGB32);
    img.fill(c);
    img.save(path, "PNG");
}
} // namespace

int main()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QDir root(dir.path());

    // Two colour images and one grey (monochrome-looking) image at the root...
    writeSolid(root.filePath(QStringLiteral("red.png")), QColor(220, 30, 30));
    writeSolid(root.filePath(QStringLiteral("blue.png")), QColor(30, 30, 220));
    writeSolid(root.filePath(QStringLiteral("grey.png")), QColor(128, 128, 128));
    // ...a non-image file that must be ignored...
    QFile notes(root.filePath(QStringLiteral("notes.txt")));
    CHECK(notes.open(QIODevice::WriteOnly));
    notes.write("not an image");
    notes.close();
    // ...and one more colour image tucked in a subfolder.
    CHECK(root.mkpath(QStringLiteral("sub")));
    writeSolid(root.filePath(QStringLiteral("sub/green.png")), QColor(30, 220, 30));

    // Non-recursive: only the 3 root images.
    {
        const imagestats::FolderStats s = imagestats::computeFolderStats(dir.path(), false);
        CHECK(s.totalImages == 3);
        CHECK(s.rawCount == 0);
        CHECK(s.rasterCount == 3);
        CHECK(s.colorCount == 2);
        CHECK(s.monoCount == 1);
        CHECK(s.unknownColorCount == 0);
        CHECK(s.folderCount == 0);
        // No RAW files, so no focal-length data.
        CHECK(s.focalKnownCount == 0);
        CHECK(s.cameras.isEmpty());
    }

    // Recursive (the default): the subfolder's image joins the count.
    {
        const imagestats::FolderStats s = imagestats::computeFolderStats(dir.path(), true);
        CHECK(s.totalImages == 4);
        CHECK(s.colorCount == 3);
        CHECK(s.monoCount == 1);
        CHECK(s.folderCount == 1);
    }

    // The fixed focal-length bucket labels are always present, even with none hit.
    {
        const imagestats::FolderStats s = imagestats::computeFolderStats(dir.path(), false);
        CHECK(s.focalBuckets.size() == 6);
        CHECK(s.focalBuckets.first().label == QStringLiteral("<24mm"));
        CHECK(s.focalBuckets.last().label == QStringLiteral("135mm+"));
        for (const auto &b : s.focalBuckets)
            CHECK(b.count == 0);
    }

    // A canceled scan stops early and undercounts — callers use this to bail
    // out of a stale background precompute without finishing the walk.
    {
        int seen = 0;
        const imagestats::FolderStats s = imagestats::computeFolderStats(
            dir.path(), true, {}, [&seen] { return ++seen > 1; });
        CHECK(s.totalImages < 4);
    }

    std::fprintf(stderr, "OK\n");
    return 0;
}
