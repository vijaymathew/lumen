#pragma once

#include <QJsonObject>
#include <QString>

class EditGraph;
class Layer;

// A reusable edit preset: the global, image-independent part of an edit — the
// Base layer's tone/colour/effect nodes plus the creative vignette — captured so
// the same "look" can be applied to a different photo. Unlike a .lumen project
// (which persists one document, source bytes and all), a preset deliberately
// omits everything photo-specific: crop, heal strokes, lens/perspective, local-
// adjustment layers and RAW decode options. This is what a .lumenpreset file
// stores and what Copy / Paste Settings moves between photos.
namespace preset {

constexpr quint32 kVersion = 1;

// Captures a preset from `graph`'s Base layer. `name` is an optional display
// label stored in the document.
QJsonObject fromGraph(const EditGraph &graph, const QString &name = QString());

// True if `obj` looks like a preset document (carries our marker key).
bool isPreset(const QJsonObject &obj);

// The preset's display name, or an empty string.
QString name(const QJsonObject &preset);

// Applies `preset` onto `graph`, overwriting the Base layer's creative nodes and
// the vignette while leaving everything else — source, crop, heal, lens and any
// local layers — untouched. Because a preset carries every creative node's state
// (even defaults), applying it fully determines the look regardless of the
// photo's prior edits. Returns false if `preset` is not a valid preset document.
bool applyToGraph(const QJsonObject &preset, EditGraph &graph);

// Applies just the preset's node parameters onto one layer's node chain (by node
// type), ignoring the graph-level vignette and any node type the layer lacks
// (e.g. grain on a non-Base layer). This is how the Presets browser lays a preset
// onto a dedicated full-coverage layer so its opacity can blend the whole look.
bool applyToLayer(const QJsonObject &preset, Layer &layer);

// A .lumenpreset file is the preset JSON written as indented UTF-8 text.
bool save(const QString &path, const QJsonObject &preset, QString *error = nullptr);
bool load(const QString &path, QJsonObject *out, QString *error = nullptr);

// Directory where the user's own .lumenpreset files live (~/.lumen/presets).
// The Presets browser scans it and "Save preset" defaults here, so a saved
// preset shows up in the browser without any extra step. Created on first
// access; returns an empty string if it cannot be created.
QString userPresetsDir();

} // namespace preset
