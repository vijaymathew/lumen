// Unit test for Telea inpainting and HealNode. Pure CPU for the algorithm;
// HealNode is exercised through libvips so it needs the runtime.

#include "core/HealNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/Inpaint.h"
#include "core/SelectiveMask.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>

#include <cmath>
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

    const int w = 48, h = 48;

    // A solid blue field with a red blemish in the middle. Inpainting the
    // blemish should fill it with the surrounding blue.
    std::vector<uint8_t> img(static_cast<size_t>(w) * h * 4);
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t *p = img.data() + (static_cast<size_t>(y) * w + x) * 4;
            const bool blemish = std::abs(x - w / 2) < 5 && std::abs(y - h / 2) < 5;
            p[0] = blemish ? 220 : 30;  // R
            p[1] = 40;                  // G
            p[2] = blemish ? 30 : 200;  // B
            p[3] = 255;
            if (blemish)
                mask[static_cast<size_t>(y) * w + x] = 255;
        }
    }

    std::vector<uint8_t> teleaImg = img; // keep a copy for the Criminisi pass
    inpaintTelea(img.data(), w, h, 4, mask, 5);

    // The centre of the blemish should now resemble the blue background, not red.
    const uint8_t *c = img.data() + ((static_cast<size_t>(h / 2) * w + w / 2)) * 4;
    CHECK(c[2] > 120); // blue restored
    CHECK(c[0] < 120); // red gone
    // A background pixel is untouched.
    const uint8_t *bg = img.data() + ((static_cast<size_t>(2) * w + 2)) * 4;
    CHECK(bg[0] == 30 && bg[2] == 200);

    // Criminisi exemplar fill: copies real blue patches, so the centre becomes
    // essentially exact background blue (no diffusion blur).
    inpaintCriminisi(teleaImg.data(), w, h, 4, mask, 4);
    const uint8_t *cc = teleaImg.data() + ((static_cast<size_t>(h / 2) * w + w / 2)) * 4;
    CHECK(cc[0] == 30 && cc[1] == 40 && cc[2] == 200); // exact blue patch copied
    const uint8_t *ccbg = teleaImg.data() + ((static_cast<size_t>(2) * w + 2)) * 4;
    CHECK(ccbg[0] == 30 && ccbg[2] == 200); // background untouched

    // HealNode end to end: paint a heal mask, apply, check the region changed.
    const QString path = QDir::temp().filePath(QStringLiteral("lumen_heal_src.png"));
    QImage src(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool blemish = std::abs(x - w / 2) < 5 && std::abs(y - h / 2) < 5;
            src.setPixelColor(x, y, blemish ? QColor(220, 40, 30) : QColor(30, 40, 200));
        }
    CHECK(src.save(path, "PNG"));
    Image in = Image::fromFile(path);
    CHECK(!in.isNull());

    HealNode node;
    // Empty mask -> passthrough.
    CHECK(node.apply(in).toQImage().pixelColor(w / 2, h / 2).red() > 200);

    MaskBuffer hm;
    hm.width = w;
    hm.height = h;
    hm.data.assign(static_cast<size_t>(w) * h, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (std::abs(x - w / 2) < 5 && std::abs(y - h / 2) < 5)
                hm.data[static_cast<size_t>(y) * w + x] = 1.0f;
    node.setHealMask(hm);

    QImage healedImg = node.apply(in).toQImage();
    QColor healed = healedImg.pixelColor(w / 2, h / 2);
    CHECK(healed.blue() > 120); // blue restored
    CHECK(healed.red() < 120);  // red gone
    // Background pixels are untouched by the heal.
    QColor healedBg = healedImg.pixelColor(2, 2);
    CHECK(healedBg.red() == 30 && healedBg.blue() == 200);

    QFile::remove(path);
    ImageBuffer::shutdownLibrary();
    std::puts("heal_test: OK");
    return 0;
}
