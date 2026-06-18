// Unit test for cursor-centred zoom (gpu/ZoomMath.h): after a zoom change, the
// image pixel under the cursor must be unchanged.

#include "gpu/ZoomMath.h"

#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool near(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) < 1e-3 && std::abs(a.y() - b.y()) < 1e-3;
}

int main()
{
    const QSizeF widget(1000, 800);
    const QSizeF image(640, 480);
    const QPointF pan0(0, 0);
    const float zoom0 = 1.0f;

    // An off-centre cursor makes the test meaningful (centre would pass trivially).
    const QPointF cursor(760, 250);
    const QPointF anchor = zoommath::imagePixelAt(widget, image, zoom0, pan0, cursor);

    // Zoom in: the same image pixel stays under the cursor.
    const float zoomIn = 2.5f;
    const QPointF panIn = zoommath::panForZoom(widget, image, zoom0, zoomIn, pan0, cursor);
    CHECK(near(zoommath::imagePixelAt(widget, image, zoomIn, panIn, cursor), anchor));

    // Zoom out: same invariant.
    const float zoomOut = 0.4f;
    const QPointF panOut = zoommath::panForZoom(widget, image, zoom0, zoomOut, pan0, cursor);
    CHECK(near(zoommath::imagePixelAt(widget, image, zoomOut, panOut, cursor), anchor));

    // Chained zooms from an already-panned/zoomed state also hold the anchor.
    const QPointF cursor2(300, 600);
    const QPointF anchor2 = zoommath::imagePixelAt(widget, image, zoomIn, panIn, cursor2);
    const QPointF panChain = zoommath::panForZoom(widget, image, zoomIn, zoomIn * 1.3f, panIn, cursor2);
    CHECK(near(zoommath::imagePixelAt(widget, image, zoomIn * 1.3f, panChain, cursor2), anchor2));

    std::puts("zoom_test: OK");
    return 0;
}
