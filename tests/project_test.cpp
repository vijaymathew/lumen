// Unit test for the .lumen project format: a layered edit graph round-trips
// through save → load (binary container + embedded source blob), and the loaded
// manifest restores the Base layer (by node type) and the non-Base layers
// (structurally) with their parameters and masks intact. Also covers that the
// white-balance (Kelvin/tint), colour-grade wheels, and the 3D LUT (embedded
// bytes — restored without the original file) survive the round-trip.

#include "core/ColorGradeNode.h"
#include "core/EditGraph.h"
#include "core/Layer.h"
#include "core/LutNode.h"
#include "core/Project.h"
#include "core/TuneNode.h"

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

static TuneNode *tuneOf(EditGraph &g, int layer)
{
    return static_cast<TuneNode *>(g.layer(layer).nodeOfType(QStringLiteral("tune")));
}
static ColorGradeNode *gradeOf(EditGraph &g, int layer)
{
    return static_cast<ColorGradeNode *>(
        g.layer(layer).nodeOfType(QStringLiteral("colorgrade")));
}
static LutNode *lutOf(EditGraph &g, int layer)
{
    return static_cast<LutNode *>(g.layer(layer).nodeOfType(QStringLiteral("lut")));
}

// Writes a 2^3 identity .cube to a temp path and returns it.
static QString writeIdentityCube()
{
    const QString p = QDir::temp().filePath(QStringLiteral("lumen_project_lut.cube"));
    QFile::remove(p);
    QFile f(p);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write("LUT_3D_SIZE 2\n");
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r) // red fastest
                f.write(QStringLiteral("%1 %2 %3\n").arg(r).arg(g).arg(b).toUtf8());
    f.close();
    return p;
}

int main()
{
    // Build a graph: Base with tune (exposure 0.5, warm WB, +tint), a colour-grade
    // (gain master + a lift puck), and a LUT loaded from a .cube; plus a
    // radial-masked adjustment layer (exposure 2.0).
    EditGraph a;
    auto *baseTune = static_cast<TuneNode *>(a.addNode(std::make_unique<TuneNode>()));
    baseTune->setExposure(0.5f);
    baseTune->setKelvin(7200.0f);
    baseTune->setTint(15.0f);
    auto *baseGrade =
        static_cast<ColorGradeNode *>(a.addNode(std::make_unique<ColorGradeNode>()));
    ColorGradeValues gv;
    gv.enabled = true;
    gv.gainMaster = 0.4f;
    gv.liftX = -0.3f;
    gv.liftY = 0.2f;
    baseGrade->setValues(gv);
    auto *baseLut = static_cast<LutNode *>(a.addNode(std::make_unique<LutNode>()));
    const QString cubePath = writeIdentityCube();
    CHECK(baseLut->loadLut(cubePath));
    baseLut->setIntensity(0.5f);

    Layer &spot = a.addLayer(QStringLiteral("Spot"));
    auto *spotTune = static_cast<TuneNode *>(a.addNode(std::make_unique<TuneNode>()));
    spotTune->setExposure(2.0f);
    MaskSpec m;
    m.type = MaskSpec::Radial;
    m.radiusX = m.radiusY = 0.2f;
    spot.setMask(m);

    const QByteArray fakeSource = QByteArrayLiteral("\xFF\xD8\xFF original jpeg bytes");
    const QString path = QDir::temp().filePath(QStringLiteral("lumen_project_test.lumen"));

    QString error;
    CHECK(project::save(path, a.saveState(), fakeSource, QStringLiteral("photo.jpg"), &error));

    // Prove the LUT is self-contained: delete the source .cube before loading.
    QFile::remove(cubePath);

    project::Project proj;
    CHECK(project::load(path, &proj, &error));
    CHECK(proj.sourceBytes == fakeSource);              // source embedded verbatim
    CHECK(proj.sourceName == QStringLiteral("photo.jpg"));

    // Load into a fresh graph whose Base already has the app's node set, so
    // restore-by-type lands the saved Base params on them.
    EditGraph b;
    b.addNode(std::make_unique<TuneNode>());
    b.addNode(std::make_unique<ColorGradeNode>());
    b.addNode(std::make_unique<LutNode>());
    b.loadProjectState(proj.graph);

    CHECK(b.layerCount() == 2);

    // Tune: exposure + white balance (Kelvin/tint).
    CHECK(tuneOf(b, 0) != nullptr);
    CHECK(tuneOf(b, 0)->exposure() == 0.5f);
    CHECK(tuneOf(b, 0)->kelvin() == 7200.0f);
    CHECK(tuneOf(b, 0)->tint() == 15.0f);

    // Colour grade wheels.
    CHECK(gradeOf(b, 0) != nullptr);
    CHECK(gradeOf(b, 0)->values() == gv);

    // LUT: rebuilt from the embedded bytes (the .cube no longer exists on disk).
    CHECK(lutOf(b, 0) != nullptr);
    CHECK(lutOf(b, 0)->lut().isValid());
    CHECK(lutOf(b, 0)->lut().dim() == 2);
    CHECK(std::abs(lutOf(b, 0)->intensity() - 0.5f) < 1e-6f);

    // Non-Base layer rebuilt structurally.
    CHECK(tuneOf(b, 1) != nullptr);
    CHECK(tuneOf(b, 1)->exposure() == 2.0f);
    CHECK(b.layer(1).name() == QStringLiteral("Spot"));
    CHECK(b.layer(1).mask().type == MaskSpec::Radial);

    // A corrupt / non-Lumen file is rejected.
    const QString bogus = QDir::temp().filePath(QStringLiteral("lumen_not_a_project.lumen"));
    {
        QFile f(bogus);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("not a lumen file at all");
    }
    project::Project bad;
    CHECK(!project::load(bogus, &bad, &error));

    QFile::remove(path);
    QFile::remove(bogus);
    std::puts("project_test: OK");
    return 0;
}
