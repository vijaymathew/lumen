// Verifies the shipped built-in presets are valid preset documents that apply
// cleanly and switch deterministically (a Colour preset clears a prior B&W
// conversion, and vice versa) — the property the Presets browser relies on.

#include "core/BuiltinPresets.h"
#include "core/ColorGradeNode.h"
#include "core/CurvesNode.h"
#include "core/EditGraph.h"
#include "core/GrainNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/MonoNode.h"
#include "core/Preset.h"
#include "core/TuneNode.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace {

// The subset of Base creative nodes the built-ins address. applyToGraph matches
// by type, so the graph must actually carry these for the values to land.
void addCreativeNodes(EditGraph &g)
{
    g.addNode(std::make_unique<TuneNode>());
    g.addNode(std::make_unique<CurvesNode>());
    g.addNode(std::make_unique<ColorGradeNode>());
    g.addNode(std::make_unique<MonoNode>());
    g.addNode(std::make_unique<GrainNode>());
}

bool monoEnabled(const EditGraph &g)
{
    auto *mono = static_cast<MonoNode *>(g.baseLayer().nodeOfType(QStringLiteral("mono")));
    return mono && mono->values().enabled;
}

} // namespace

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Redirect the "home" dir (which userPresetsDir() derives from) to a scratch
    // location so the test never reads or writes the real ~/.lumen/presets.
    const QString home = QDir(QDir::tempPath()).filePath(QStringLiteral("lumen_preset_test_home"));
    QDir(home).removeRecursively();
    CHECK(QDir().mkpath(home));
    CHECK(setenv("HOME", home.toUtf8().constData(), 1) == 0);

    const QVector<preset::Builtin> builtins = preset::builtins();
    CHECK(!builtins.isEmpty());

    // With an empty presets dir, the library is exactly the built-ins.
    CHECK(preset::userPresets().isEmpty());
    CHECK(preset::library().size() == builtins.size());

    // Every built-in is a well-formed, named, categorised preset document.
    const preset::Builtin *bw = nullptr;
    const preset::Builtin *color = nullptr;
    for (const preset::Builtin &b : builtins) {
        CHECK(!b.id.isEmpty());
        CHECK(!b.name.isEmpty());
        CHECK(preset::isPreset(b.data));
        CHECK(preset::name(b.data) == b.name);
        if (b.category == QStringLiteral("B&W") && !bw)
            bw = &b;
        if (b.category == QStringLiteral("Color") && !color)
            color = &b;
    }
    CHECK(bw != nullptr);
    CHECK(color != nullptr);

    EditGraph g;
    g.setSource(Image::black(16, 16));
    addCreativeNodes(g);

    // Applying a B&W preset turns the mono conversion on; applying a Colour preset
    // afterwards turns it back off — the deterministic-switch property.
    CHECK(preset::applyToGraph(bw->data, g));
    CHECK(monoEnabled(g));
    CHECK(!g.result().isNull());

    CHECK(preset::applyToGraph(color->data, g));
    CHECK(!monoEnabled(g));
    CHECK(!g.result().isNull());

    // And back to B&W once more, to confirm it's not a one-way transition.
    CHECK(preset::applyToGraph(bw->data, g));
    CHECK(monoEnabled(g));

    // --- fromGraph records the category ------------------------------------
    // Graph currently has the B&W preset applied (mono on).
    CHECK(preset::fromGraph(g).value(QStringLiteral("category")).toString()
          == QStringLiteral("B&W"));
    CHECK(preset::applyToGraph(color->data, g)); // switch to a colour look (mono off)
    CHECK(preset::fromGraph(g).value(QStringLiteral("category")).toString()
          == QStringLiteral("Color"));

    // --- User presets fold into the library ---------------------------------
    const QString dir = preset::userPresetsDir();
    CHECK(!dir.isEmpty());
    // Save one B&W and one Colour user preset, each carrying its category.
    QJsonObject bwSaved = bw->data;
    bwSaved[QStringLiteral("name")] = QStringLiteral("My Mono");
    bwSaved[QStringLiteral("category")] = QStringLiteral("B&W");
    QJsonObject colorSaved = color->data;
    colorSaved[QStringLiteral("name")] = QStringLiteral("My Colour");
    colorSaved[QStringLiteral("category")] = QStringLiteral("Color");
    CHECK(preset::save(QDir(dir).filePath(QStringLiteral("mine_bw.lumenpreset")), bwSaved, nullptr));
    CHECK(preset::save(QDir(dir).filePath(QStringLiteral("mine_color.lumenpreset")), colorSaved,
                       nullptr));

    const QVector<preset::Builtin> users = preset::userPresets();
    CHECK(users.size() == 2);
    for (const preset::Builtin &u : users) {
        CHECK(preset::isPreset(u.data));
        CHECK(u.category == QStringLiteral("B&W") || u.category == QStringLiteral("Color"));
    }

    const QVector<preset::Builtin> lib = preset::library();
    CHECK(lib.size() == builtins.size() + 2);
    // Grouped by section: ranks are non-decreasing (B&W < Color < other), so
    // sections never interleave. And within B&W a built-in precedes the user one.
    int lastRank = -1;
    bool sawUserBw = false;
    for (const preset::Builtin &b : lib) {
        const int r = b.category == QStringLiteral("B&W")     ? 0
                      : b.category == QStringLiteral("Color") ? 1
                                                              : 2;
        CHECK(r >= lastRank);
        lastRank = r;
        const bool isUser = b.id.startsWith(QStringLiteral("user:"));
        if (b.category == QStringLiteral("B&W")) {
            if (isUser)
                sawUserBw = true;
            else
                CHECK(!sawUserBw); // built-in B&W must come before the user B&W
        }
    }
    CHECK(sawUserBw);

    // The folded-in user presets apply like any other.
    for (const preset::Builtin &u : users)
        CHECK(preset::applyToGraph(u.data, g));
    CHECK(!g.result().isNull());

    // --- Rename & delete via the id → path mapping --------------------------
    // Built-in ids don't resolve to a file; user ids do.
    CHECK(preset::userPresetPath(bw->id).isEmpty());
    const QString onePath = preset::userPresetPath(users[0].id);
    CHECK(!onePath.isEmpty());
    CHECK(QDir(dir).exists(QFileInfo(onePath).fileName()));

    // Rename = rewrite the file's "name"; the id (path) is unchanged.
    {
        QJsonObject obj;
        CHECK(preset::load(onePath, &obj, nullptr));
        obj[QStringLiteral("name")] = QStringLiteral("Renamed");
        CHECK(preset::save(onePath, obj, nullptr));
    }
    bool foundRenamed = false;
    for (const preset::Builtin &u : preset::userPresets())
        if (u.id == users[0].id && u.name == QStringLiteral("Renamed"))
            foundRenamed = true;
    CHECK(foundRenamed);

    // Delete removes it from the library.
    CHECK(QFile::remove(onePath));
    CHECK(preset::userPresets().size() == 1);
    CHECK(preset::library().size() == builtins.size() + 1);

    // --- Amount blend: a full-coverage preset layer's opacity interpolates ---
    // (The Presets browser applies a preset this way so "Amount" blends the whole
    // look, including the B&W conversion, toward the original.)
    {
        const int W = 8, H = 8;
        std::vector<unsigned char> rgb(static_cast<size_t>(W) * H * 3);
        for (int i = 0; i < W * H; ++i) { // saturated red
            rgb[i * 3 + 0] = 210;
            rgb[i * 3 + 1] = 40;
            rgb[i * 3 + 2] = 40;
        }
        Image red = Image::fromInterleaved(rgb.data(), W, H, 3);
        CHECK(!red.isNull());

        const auto meanSat = [](const Image &img) {
            const QImage q = img.toQImage();
            double s = 0;
            int n = 0;
            for (int y = 0; y < q.height(); ++y)
                for (int x = 0; x < q.width(); ++x) {
                    const QColor c = q.pixelColor(x, y);
                    s += std::abs(c.red() - c.green()) + std::abs(c.green() - c.blue());
                    ++n;
                }
            return n ? s / n : 0.0;
        };

        EditGraph bg;
        bg.setSource(red);
        Layer &pl = bg.addLayer(QStringLiteral("Preset")); // full-coverage (None mask)
        bg.addNode(std::make_unique<TuneNode>());
        bg.addNode(std::make_unique<CurvesNode>());
        bg.addNode(std::make_unique<ColorGradeNode>());
        bg.addNode(std::make_unique<MonoNode>());
        bg.addNode(std::make_unique<GrainNode>());
        CHECK(preset::applyToLayer(bw->data, pl)); // a B&W preset

        pl.setOpacity(0.0f);
        const double sat0 = meanSat(bg.result());
        pl.setOpacity(1.0f);
        const double sat1 = meanSat(bg.result());
        pl.setOpacity(0.5f);
        const double satHalf = meanSat(bg.result());

        CHECK(sat0 > 30.0);                       // amount 0 → original colour kept
        CHECK(sat1 < sat0 * 0.5);                 // amount 100 → desaturated (B&W)
        CHECK(satHalf < sat0 && satHalf > sat1);  // amount 50 → between the two
    }

    QDir(home).removeRecursively(); // tidy up the scratch home
    std::printf("OK: %d built-in + %d user presets\n", static_cast<int>(builtins.size()),
                static_cast<int>(users.size()));
    return 0;
}
