// Unit test for the .lumen project format: a layered edit graph round-trips
// through save → load (binary container + embedded source blob), and the loaded
// manifest restores the Base layer (by node type) and the non-Base layers
// (structurally) with their parameters and masks intact.

#include "core/EditGraph.h"
#include "core/Layer.h"
#include "core/Project.h"
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

static TuneNode *tuneOf(EditGraph &g, int layer)
{
    return static_cast<TuneNode *>(g.layer(layer).nodeOfType(QStringLiteral("tune")));
}

int main()
{
    // Build a graph: Base with a tune node (exposure 0.5) + a radial-masked
    // adjustment layer (exposure 2.0).
    EditGraph a;
    auto *baseTune = static_cast<TuneNode *>(a.addNode(std::make_unique<TuneNode>()));
    baseTune->setExposure(0.5f);
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

    project::Project proj;
    CHECK(project::load(path, &proj, &error));
    CHECK(proj.sourceBytes == fakeSource);              // source embedded verbatim
    CHECK(proj.sourceName == QStringLiteral("photo.jpg"));

    // Load into a fresh graph whose Base already has a tune node (as the app's
    // Base does). Restore-by-type must land the saved Base params on it.
    EditGraph b;
    b.addNode(std::make_unique<TuneNode>()); // Base tune
    b.loadProjectState(proj.graph);

    CHECK(b.layerCount() == 2);
    CHECK(tuneOf(b, 0) != nullptr);
    CHECK(tuneOf(b, 0)->exposure() == 0.5f);            // Base restored by type
    CHECK(tuneOf(b, 1) != nullptr);
    CHECK(tuneOf(b, 1)->exposure() == 2.0f);            // layer rebuilt structurally
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
