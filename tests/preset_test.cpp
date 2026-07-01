// Exercises reusable edit presets: capture the creative edit from one graph,
// round-trip through a .lumenpreset file, and apply it to a *different* graph —
// verifying the look transfers while photo-specific state (crop) does not.

#include "core/CropState.h"
#include "core/EditGraph.h"
#include "core/HealNode.h"
#include "core/ImageBuffer.h"
#include "core/Preset.h"
#include "core/TuneNode.h"
#include "core/Vignette.h"

#include <QDir>
#include <QFile>

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

// The Base layer owns one node per creative type; fetch the tune node.
static TuneNode *baseTune(EditGraph &g)
{
    for (int i = 0; i < g.baseLayer().nodeCount(); ++i)
        if (auto *t = dynamic_cast<TuneNode *>(g.baseLayer().nodeAt(i)))
            return t;
    return nullptr;
}

// Build a graph shaped like the app's: a Base layer carrying the full node set.
static void seedBaseNodes(EditGraph &g)
{
    g.addNode(std::make_unique<HealNode>()); // a photo-specific node (excluded)
    g.addNode(std::make_unique<TuneNode>());
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // --- Source graph: give it a distinctive look + a crop -------------------
    EditGraph a;
    a.setSource(Image::black(32, 24));
    seedBaseNodes(a);
    baseTune(a)->setExposure(1.5f);
    baseTune(a)->setContrast(40.0f);
    VignetteParams vig;
    vig.amount = -35.0f;
    a.setVignette(vig);
    CropState crop;
    crop.rect = QRectF(0.1, 0.1, 0.5, 0.5); // photo-specific — must NOT transfer
    a.setCrop(crop);

    // --- Capture + file round-trip -------------------------------------------
    const QJsonObject captured = preset::fromGraph(a, QStringLiteral("Punchy"));
    CHECK(preset::isPreset(captured));
    CHECK(preset::name(captured) == QStringLiteral("Punchy"));

    const QString path = QDir::temp().filePath(QStringLiteral("lumen_test.lumenpreset"));
    QFile::remove(path);
    QString error;
    CHECK(preset::save(path, captured, &error));
    QJsonObject loaded;
    CHECK(preset::load(path, &loaded, &error));
    CHECK(loaded == captured);
    QFile::remove(path);

    // Loading a non-preset must fail cleanly.
    const QString junk = QDir::temp().filePath(QStringLiteral("lumen_notpreset.txt"));
    QFile jf(junk);
    CHECK(jf.open(QIODevice::WriteOnly));
    jf.write("{\"hello\":1}");
    jf.close();
    QJsonObject dummy;
    CHECK(!preset::load(junk, &dummy, &error));
    QFile::remove(junk);

    // --- Apply to a fresh graph ----------------------------------------------
    EditGraph b;
    b.setSource(Image::black(64, 48)); // a different photo (different size)
    seedBaseNodes(b);
    // b starts neutral, with its own crop that the preset must leave alone.
    CropState bCrop;
    bCrop.rect = QRectF(0.0, 0.0, 0.8, 0.8);
    b.setCrop(bCrop);
    CHECK(std::abs(baseTune(b)->exposure()) < 1e-6f);

    CHECK(preset::applyToGraph(loaded, b));

    // The look transferred…
    CHECK(std::abs(baseTune(b)->exposure() - 1.5f) < 1e-6f);
    CHECK(std::abs(baseTune(b)->contrast() - 40.0f) < 1e-6f);
    CHECK(std::abs(b.vignette().amount - (-35.0f)) < 1e-6f);
    // …but b's own crop is untouched (presets carry no geometry).
    CHECK(b.crop().rect == bCrop.rect);
    CHECK(b.result().width() > 0); // pipeline still composites cleanly

    // --- Determinism: applying a neutral preset resets an edited target ------
    EditGraph neutral;
    neutral.setSource(Image::black(8, 8));
    seedBaseNodes(neutral);
    const QJsonObject neutralPreset = preset::fromGraph(neutral);

    EditGraph edited;
    edited.setSource(Image::black(8, 8));
    seedBaseNodes(edited);
    baseTune(edited)->setExposure(2.0f);
    VignetteParams ev;
    ev.amount = 50.0f;
    edited.setVignette(ev);
    CHECK(preset::applyToGraph(neutralPreset, edited));
    CHECK(std::abs(baseTune(edited)->exposure()) < 1e-6f);   // exposure cleared
    CHECK(edited.vignette().isIdentity());                   // vignette cleared

    ImageBuffer::shutdownLibrary();
    std::puts("preset_test: OK");
    return 0;
}
