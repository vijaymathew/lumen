// Unit test for the RAW loader's routing and robustness. The actual demosaic
// needs a real camera-RAW file (none ship with the repo), so this covers
// extension classification and graceful failure on non-RAW data.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/RawLoader.h"

#include <QString>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(int /*argc*/, char **argv)
{
    // Extension classification (case-insensitive, ignores path/basename).
    CHECK(raw::isRawPath(QStringLiteral("photo.CR2")));
    CHECK(raw::isRawPath(QStringLiteral("/a/b/IMG_0001.nef")));
    CHECK(raw::isRawPath(QStringLiteral("x.dng")));
    CHECK(raw::isRawPath(QStringLiteral("shot.arw")));
    CHECK(!raw::isRawPath(QStringLiteral("photo.jpg")));
    CHECK(!raw::isRawPath(QStringLiteral("photo.png")));
    CHECK(!raw::isRawPath(QStringLiteral("project.lumen")));
    CHECK(!raw::isRawPath(QStringLiteral("noext")));

    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Non-RAW bytes fail gracefully: null Image + an error message, no crash.
    const char junk[] = "this is definitely not a camera raw file";
    QString error;
    Image img = raw::decodeBytes(junk, sizeof(junk), &error);
    CHECK(img.isNull());
    CHECK(!error.isEmpty());

    // Null / empty input is rejected too.
    error.clear();
    CHECK(raw::decodeBytes(nullptr, 0, &error).isNull());
    CHECK(!error.isEmpty());

    ImageBuffer::shutdownLibrary();
    std::puts("raw_test: OK");
    return 0;
}
