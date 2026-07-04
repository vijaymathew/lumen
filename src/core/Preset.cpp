#include "core/Preset.h"

#include "core/EditGraph.h"
#include "core/EditNode.h"
#include "core/Layer.h"
#include "core/MonoNode.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace preset {
namespace {

// Marker + version key: its presence identifies a preset document.
constexpr QLatin1String kMarker{"lumenPreset"};

// The node types a preset carries: global, image-independent adjustments. The
// photo-specific nodes — "heal" (pixel-space strokes) and "lens" (EXIF-driven
// perspective/geometry) — are intentionally excluded, so a preset transfers a
// look rather than another image's repairs or geometry.
bool isCreativeType(const QString &type)
{
    static const QString kCreative[] = {
        QStringLiteral("tune"),       QStringLiteral("curves"),
        QStringLiteral("colormixer"), QStringLiteral("colorgrade"),
        QStringLiteral("lut"),        QStringLiteral("mono"),
        QStringLiteral("grain"),      QStringLiteral("sharpen"),
        QStringLiteral("denoise"),    QStringLiteral("defringe"),
    };
    for (const QString &t : kCreative)
        if (type == t)
            return true;
    return false;
}

} // namespace

QJsonObject fromGraph(const EditGraph &graph, const QString &name)
{
    QJsonObject root;
    root[kMarker] = static_cast<int>(kVersion);
    if (!name.isEmpty())
        root[QStringLiteral("name")] = name;

    const Layer &base = graph.baseLayer();
    QJsonArray nodes;
    bool monochrome = false;
    for (int i = 0; i < base.nodeCount(); ++i) {
        const EditNode *n = base.nodeAt(i);
        if (!n || !isCreativeType(n->typeName()))
            continue;
        // The look is "B&W" when the monochrome conversion is on; used by the
        // Presets browser to file the preset under the right section.
        if (n->typeName() == QLatin1String("mono"))
            monochrome = static_cast<const MonoNode *>(n)->values().enabled;
        QJsonObject e;
        e[QStringLiteral("type")] = n->typeName();
        e[QStringLiteral("state")] = n->saveState();
        nodes.append(e);
    }
    root[QStringLiteral("nodes")] = nodes;
    root[QStringLiteral("category")] =
        monochrome ? QStringLiteral("B&W") : QStringLiteral("Color");
    // Store the vignette unconditionally (even identity) so applying a preset is
    // deterministic: it clears a target's existing vignette when the preset has none.
    root[QStringLiteral("vignette")] = graph.vignette().toJson();
    return root;
}

bool isPreset(const QJsonObject &obj)
{
    return obj.contains(kMarker);
}

QString name(const QJsonObject &preset)
{
    return preset.value(QStringLiteral("name")).toString();
}

bool applyToLayer(const QJsonObject &preset, Layer &layer)
{
    if (!isPreset(preset))
        return false;

    for (const QJsonValue &v : preset.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString type = e.value(QStringLiteral("type")).toString();
        if (!isCreativeType(type))
            continue; // ignore anything unexpected in the file
        // Only the node types this layer carries are touched; e.g. a non-Base
        // layer has no grain node, so grain in the preset is silently skipped.
        if (EditNode *node = layer.nodeOfType(type))
            node->restoreState(e.value(QStringLiteral("state")).toObject());
    }
    layer.invalidateFrom(0); // parameters changed — recompute the whole chain
    return true;
}

bool applyToGraph(const QJsonObject &preset, EditGraph &graph)
{
    if (!applyToLayer(preset, graph.baseLayer()))
        return false;
    // The vignette is a graph-level (post-composite) stage, so it applies here
    // rather than in applyToLayer, which only touches a layer's node chain.
    graph.setVignette(vignetteOf(preset));
    return true;
}

VignetteParams vignetteOf(const QJsonObject &preset)
{
    return VignetteParams::fromJson(preset.value(QStringLiteral("vignette")).toObject());
}

bool save(const QString &path, const QJsonObject &preset, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(preset).toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool load(const QString &path, QJsonObject *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Not a valid preset file");
        return false;
    }
    const QJsonObject obj = doc.object();
    if (!isPreset(obj)) {
        if (error)
            *error = QStringLiteral("Not a Lumen preset");
        return false;
    }
    if (out)
        *out = obj;
    return true;
}

QString userPresetsDir()
{
    // Mirrors autosave::projectsDir()'s ~/.lumen home so all app data sits together.
    const QString path = QDir(QDir::homePath()).filePath(QStringLiteral(".lumen/presets"));
    if (!QDir().mkpath(path))
        return QString();
    return path;
}

} // namespace preset
