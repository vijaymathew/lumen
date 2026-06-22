// Unit test for LutNode: identity passthrough, an inverting HALD CLUT applied
// to a known image, and look exposure through the graph.

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/Lut3D.h"
#include "core/LutNode.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>

#include <cmath>
#include <cstdio>
#include <memory>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// Inverting HALD CLUT: output colour = 1 - input colour.
static QImage makeInvertHald(int dim)
{
    const int side = static_cast<int>(std::lround(std::sqrt(double(dim) * dim * dim)));
    QImage img(side, side, QImage::Format_RGBA8888);
    for (int p = 0; p < side * side; ++p) {
        const int r = p % dim, g = (p / dim) % dim, b = p / (dim * dim);
        const auto inv = [dim](int i) {
            return static_cast<int>(std::lround((1.0 - i / double(dim - 1)) * 255.0));
        };
        img.setPixelColor(p % side, p / side, QColor(inv(r), inv(g), inv(b)));
    }
    return img;
}

static bool near8(int a, int b) { return std::abs(a - b) <= 3; }

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    const QDir tmp = QDir::temp();
    const QString imgPath = tmp.filePath(QStringLiteral("lumen_lutnode_src.png"));
    const QString haldPath = tmp.filePath(QStringLiteral("lumen_lutnode_hald.png"));

    QImage solid(4, 4, QImage::Format_RGBA8888);
    solid.fill(QColor(200, 100, 50));
    CHECK(solid.save(imgPath, "PNG"));
    CHECK(makeInvertHald(4).save(haldPath, "PNG"));

    Image src = Image::fromFile(imgPath);
    CHECK(!src.isNull());

    // Identity (no LUT) is a passthrough.
    LutNode node;
    Image pass = node.apply(src);
    QImage passImg = pass.toQImage();
    QColor pc = passImg.pixelColor(0, 0);
    CHECK(near8(pc.red(), 200) && near8(pc.green(), 100) && near8(pc.blue(), 50));

    // Apply the inverting CLUT.
    CHECK(node.loadHald(haldPath));
    CHECK(node.lut().isValid());
    Image inv = node.apply(src);
    QColor ic = inv.toQImage().pixelColor(0, 0);
    CHECK(near8(ic.red(), 55) && near8(ic.green(), 155) && near8(ic.blue(), 205));

    // The graph exposes the look for the preview path.
    EditGraph graph;
    auto *gnode = static_cast<LutNode *>(graph.addNode(std::make_unique<LutNode>()));
    CHECK(!graph.previewLook().isValid()); // no look yet
    CHECK(gnode->loadHald(haldPath));
    CHECK(graph.previewLook().isValid());
    CHECK(graph.previewLook().dim() == 4);

    // Disabled look node contributes nothing.
    gnode->setEnabled(false);
    CHECK(!graph.previewLook().isValid());

    QFile::remove(imgPath);
    QFile::remove(haldPath);
    ImageBuffer::shutdownLibrary();
    std::puts("lutnode_test: OK");
    return 0;
}
