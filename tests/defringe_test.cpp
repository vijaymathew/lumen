// Unit test for DefringeNode: a purple fringe pixel along a high-contrast edge is
// desaturated (its red/green gap shrinks), a flat-region pixel is left alone,
// disabled is a passthrough, and the Values JSON round-trips.

#include "core/DefringeNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"

#include <QImage>
#include <QJsonObject>

#include <cstdint>
#include <cstdio>
#include <vector>

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

    // A vertical high-contrast edge: dark left, bright right. The boundary column
    // (x==fringeX) carries a saturated purple fringe.
    const int w = 24, h = 12, fringeX = 11;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            uint8_t r, g, b;
            if (x == fringeX) {
                r = 200; g = 60; b = 200; // purple/magenta fringe
            } else if (x < fringeX) {
                r = g = b = 40; // dark
            } else {
                r = g = b = 220; // bright
            }
            px[i + 0] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255;
        }
    }
    Image src = Image::fromInterleaved(px.data(), w, h, 4);

    // 1. Disabled = passthrough.
    {
        DefringeNode node;
        DefringeNode::Values v; // disabled
        node.setValues(v);
        Image out = node.apply(src);
        QImage q = out.toQImage();
        CHECK(qRed(q.pixel(fringeX, 6)) == 200 && qGreen(q.pixel(fringeX, 6)) == 60);
    }

    // 2. Enabled (purple) desaturates the fringe pixel: R−G gap shrinks markedly,
    //    while a flat dark pixel away from the edge is essentially untouched.
    {
        DefringeNode node;
        DefringeNode::Values v;
        v.enabled = true;
        v.purple = 100.0f;
        v.green = 0.0f;
        v.threshold = 0.0f;
        node.setValues(v);
        Image out = node.apply(src);
        QImage q = out.toQImage();

        const QColor fringe = q.pixelColor(fringeX, 6);
        const int gapBefore = 200 - 60; // 140
        const int gapAfter = fringe.red() - fringe.green();
        CHECK(gapAfter < gapBefore - 30); // visibly desaturated toward neutral

        const QColor flat = q.pixelColor(2, 6); // dark flat region, no edge/fringe
        CHECK(std::abs(flat.red() - 40) <= 6);
        CHECK(std::abs(flat.green() - 40) <= 6);
        CHECK(std::abs(flat.blue() - 40) <= 6);
    }

    // 3. JSON round-trip.
    {
        DefringeNode node;
        DefringeNode::Values v;
        v.enabled = true; v.purple = 70.0f; v.green = 35.0f; v.threshold = 40.0f;
        node.setValues(v);
        QJsonObject json = node.saveState();
        DefringeNode restored;
        restored.restoreState(json);
        CHECK(restored.values() == v);
    }

    std::printf("defringe_test OK\n");
    return 0;
}
