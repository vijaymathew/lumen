// Unit test for SelectiveNode: the luminosity-range mask gates the adjustment,
// neutral passthrough, and preview-state contribution through the graph.

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/SelectiveNode.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>

#include <cstdio>
#include <memory>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool near8(int a, int b) { return std::abs(a - b) <= 3; }

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Mid-grey image: luminance 0.5.
    const QString path = QDir::temp().filePath(QStringLiteral("lumen_selective_src.png"));
    QImage gray(4, 4, QImage::Format_RGBA8888);
    gray.fill(QColor(128, 128, 128));
    CHECK(gray.save(path, "PNG"));
    Image src = Image::fromFile(path);
    CHECK(!src.isNull());

    SelectiveNode node;

    // Neutral (no adjustment) is a passthrough even with a mask set.
    SelectiveValues v;
    v.low = 0.3f;
    v.high = 0.7f;
    v.feather = 0.05f;
    node.setValues(v);
    CHECK(near8(node.apply(src).toQImage().pixelColor(0, 0).red(), 128));

    // Mask excludes the mid-grey (shadows-only range): +2 EV has no effect.
    v = SelectiveValues{};
    v.low = 0.0f;
    v.high = 0.4f;
    v.feather = 0.05f;
    v.exposure = 2.0f;
    node.setValues(v);
    CHECK(near8(node.apply(src).toQImage().pixelColor(0, 0).red(), 128));

    // Mask includes the mid-grey (midtone range): +2 EV brightens it.
    v.low = 0.3f;
    v.high = 0.7f;
    node.setValues(v);
    const int lit = node.apply(src).toQImage().pixelColor(0, 0).red();
    CHECK(lit > 200); // 128 * 2^(2/2.2) ≈ 240

    // Preview-state contribution through the graph.
    EditGraph graph;
    auto *gnode = static_cast<SelectiveNode *>(
        graph.addNode(std::make_unique<SelectiveNode>()));
    CHECK(graph.previewState().selEnabled == 0.0f); // node neutral... but enabled
    SelectiveValues gv;
    gv.low = 0.2f;
    gv.high = 0.8f;
    gv.exposure = 1.0f;
    gnode->setValues(gv);
    const PreviewState ps = graph.previewState();
    CHECK(ps.selEnabled == 1.0f);
    CHECK(ps.selLow == 0.2f && ps.selHigh == 0.8f);
    CHECK(ps.selExposure == 1.0f);

    // Disabled node contributes nothing.
    gnode->setEnabled(false);
    CHECK(graph.previewState().selEnabled == 0.0f);

    QFile::remove(path);
    ImageBuffer::shutdownLibrary();
    std::puts("selective_test: OK");
    return 0;
}
