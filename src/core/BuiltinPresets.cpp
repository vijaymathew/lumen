#include "core/BuiltinPresets.h"

#include "core/ColorGradeNode.h"
#include "core/CurvesNode.h"
#include "core/EditNode.h"
#include "core/GrainNode.h"
#include "core/MonoNode.h"
#include "core/Preset.h"
#include "core/TuneNode.h"
#include "core/Vignette.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>

namespace preset {
namespace {

// Prefix that marks a library entry as a user preset and carries its file path.
const QString kUserPrefix = QStringLiteral("user:");

// One "nodes" array element: {type, state} from a live node's own serialisation.
QJsonObject entry(const EditNode &n)
{
    QJsonObject e;
    e[QStringLiteral("type")] = n.typeName();
    e[QStringLiteral("state")] = n.saveState();
    return e;
}

// Wraps a configured node set + vignette into a preset::fromGraph-format document.
QJsonObject assemble(const QString &name, const TuneNode &tune, const CurvesNode &curves,
                     const ColorGradeNode &grade, const MonoNode &mono, const GrainNode &grain,
                     const VignetteParams &vig)
{
    QJsonArray nodes;
    // Emit every creative node the built-ins touch, always — even at defaults — so
    // one preset cleanly overrides another (matches fromGraph's "complete" contract).
    nodes.append(entry(tune));
    nodes.append(entry(curves));
    nodes.append(entry(grade));
    nodes.append(entry(mono));
    nodes.append(entry(grain));

    QJsonObject root;
    root[QStringLiteral("lumenPreset")] = static_cast<int>(kVersion);
    root[QStringLiteral("name")] = name;
    root[QStringLiteral("nodes")] = nodes;
    root[QStringLiteral("vignette")] = vig.toJson();
    return root;
}

// --- B&W: Silver — High Contrast -------------------------------------------
// Red-filter B&W with a punchy tone curve, fine grain and a darkening vignette.
Builtin bwHighContrast()
{
    TuneNode tune;
    tune.setContrast(38);
    tune.setHighlights(-4);
    tune.setShadows(-6);
    tune.setWhites(14);
    tune.setBlacks(-20);

    MonoNode mono;
    MonoValues mv;
    mv.enabled = true; // convert to B&W
    // Classic red-filter response: lift warm tones, drop skies. Bands are at
    // 0/45/…/315° = Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta.
    mv.band[0] = 0.30f;  // Red
    mv.band[1] = 0.30f;  // Orange
    mv.band[2] = 0.18f;  // Yellow
    mv.band[3] = -0.10f; // Green
    mv.band[4] = -0.42f; // Aqua
    mv.band[5] = -0.50f; // Blue
    mv.band[6] = -0.22f; // Purple
    mono.setValues(mv);

    GrainNode grain;
    GrainNode::Values gv;
    gv.enabled = true;
    gv.amount = 32.0f;
    gv.size = 2.5f;
    grain.setValues(gv);

    VignetteParams vig;
    vig.enabled = true;
    vig.amount = -24.0f;
    vig.midpoint = 44.0f;
    vig.feather = 55.0f;

    return {QStringLiteral("bw-high-contrast"), QStringLiteral("Silver — High Contrast"),
            QStringLiteral("B&W"),
            assemble(QStringLiteral("Silver — High Contrast"), tune, CurvesNode{},
                     ColorGradeNode{}, mono, grain, vig)};
}

// --- B&W: Silver — Soft Toned ----------------------------------------------
// Gentle contrast, lifted blacks and a split-tone (cool shadows, warm highs).
Builtin bwSoftToned()
{
    TuneNode tune;
    tune.setContrast(8);
    tune.setHighlights(-6);
    tune.setShadows(6);
    tune.setBlacks(8);

    MonoNode mono;
    MonoValues mv;
    mv.enabled = true;
    mv.shadowHue = 214.0f; // cool shadows
    mv.shadowSat = 0.10f;
    mv.highHue = 42.0f; // warm highlights (selenium/sepia feel)
    mv.highSat = 0.14f;
    mono.setValues(mv);

    GrainNode grain;
    GrainNode::Values gv;
    gv.enabled = true;
    gv.amount = 16.0f;
    gv.size = 2.0f;
    grain.setValues(gv);

    VignetteParams vig;
    vig.enabled = true;
    vig.amount = -12.0f;
    vig.midpoint = 50.0f;
    vig.feather = 60.0f;

    return {QStringLiteral("bw-soft-toned"), QStringLiteral("Silver — Soft Toned"),
            QStringLiteral("B&W"),
            assemble(QStringLiteral("Silver — Soft Toned"), tune, CurvesNode{},
                     ColorGradeNode{}, mono, grain, vig)};
}

// --- Color: Warm Portrait ---------------------------------------------------
// A flattering warm look: gentle contrast, vibrance, warm highlights / cool
// shadows via the grade wheels.
Builtin colorWarmPortrait()
{
    TuneNode tune;
    tune.setExposure(0.08f);
    tune.setContrast(10);
    tune.setSaturation(4);
    tune.setVibrance(18);
    tune.setBlacks(-4);

    ColorGradeNode grade;
    ColorGradeValues gv;
    gv.enabled = true;
    gv.gainX = 0.12f; // warm highlights
    gv.gainY = 0.05f;
    gv.liftX = -0.06f; // slightly cool shadows
    gv.liftY = -0.04f;
    grade.setValues(gv);

    VignetteParams vig;
    vig.enabled = true;
    vig.amount = -10.0f;
    vig.midpoint = 55.0f;
    vig.feather = 60.0f;

    return {QStringLiteral("color-warm-portrait"), QStringLiteral("Warm Portrait"),
            QStringLiteral("Color"),
            assemble(QStringLiteral("Warm Portrait"), tune, CurvesNode{}, grade, MonoNode{},
                     GrainNode{}, vig)};
}

// --- Color: Faded Film ------------------------------------------------------
// Matte, low-contrast film look: lifted blacks, muted saturation, a teal/green
// shadow push with warm highlights, and light grain.
Builtin colorFadedFilm()
{
    TuneNode tune;
    tune.setContrast(-12);
    tune.setHighlights(-8);
    tune.setBlacks(16); // lifted, "faded" shadows
    tune.setSaturation(-12);
    tune.setVibrance(6);

    ColorGradeNode grade;
    ColorGradeValues gv;
    gv.enabled = true;
    gv.liftX = -0.05f; // green/teal shadows
    gv.liftY = 0.06f;
    gv.gainX = 0.06f; // warm highlights
    gv.gainY = 0.03f;
    gv.gammaMaster = 0.02f;
    grade.setValues(gv);

    GrainNode grain;
    GrainNode::Values grv;
    grv.enabled = true;
    grv.amount = 12.0f;
    grv.size = 2.0f;
    grain.setValues(grv);

    VignetteParams vig;
    vig.enabled = true;
    vig.amount = -8.0f;
    vig.midpoint = 55.0f;
    vig.feather = 60.0f;

    return {QStringLiteral("color-faded-film"), QStringLiteral("Faded Film"),
            QStringLiteral("Color"),
            assemble(QStringLiteral("Faded Film"), tune, CurvesNode{}, grade, MonoNode{}, grain,
                     vig)};
}

} // namespace

QVector<Builtin> builtins()
{
    return {bwHighContrast(), bwSoftToned(), colorWarmPortrait(), colorFadedFilm()};
}

QVector<Builtin> userPresets()
{
    QVector<Builtin> out;
    const QString dir = userPresetsDir();
    if (dir.isEmpty())
        return out;

    const QFileInfoList files =
        QDir(dir).entryInfoList({QStringLiteral("*.lumenpreset")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        QJsonObject data;
        if (!load(fi.absoluteFilePath(), &data, nullptr))
            continue; // skip anything that isn't a valid preset
        const QString label =
            name(data).isEmpty() ? fi.completeBaseName() : name(data);
        // Slot the preset into the section its saved category names (B&W / Color);
        // presets written before categories existed fall back to "My Presets".
        QString category = data.value(QStringLiteral("category")).toString();
        if (category.isEmpty())
            category = QStringLiteral("My Presets");
        // The absolute path is a stable, unique id (survives duplicate names).
        out.push_back({kUserPrefix + fi.absoluteFilePath(), label, category, data});
    }
    return out;
}

QVector<Builtin> library()
{
    QVector<Builtin> all = builtins();
    all += userPresets();
    // Group by section so each category header appears once, with built-ins ahead
    // of user presets within a section. A stable sort preserves that relative
    // order (built-ins are appended first) and the authored order inside each.
    const auto rank = [](const QString &category) {
        if (category == QStringLiteral("B&W"))
            return 0;
        if (category == QStringLiteral("Color"))
            return 1;
        return 2; // "My Presets" and any other custom sections come last
    };
    std::stable_sort(all.begin(), all.end(), [&](const Builtin &a, const Builtin &b) {
        return rank(a.category) < rank(b.category);
    });
    return all;
}

QString userPresetPath(const QString &id)
{
    return id.startsWith(kUserPrefix) ? id.mid(kUserPrefix.size()) : QString();
}

} // namespace preset
