#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

// A small, curated set of shipped "looks" (Nik-style B&W and colour presets),
// authored in code so their JSON always matches the current node serialisation.
// Each entry's `data` is a preset::fromGraph-format document, so it applies
// through the exact same preset::applyToGraph path as a user's .lumenpreset file.
//
// This is the content backing the Presets browser (a first cut / prototype).
// Every preset carries the same set of creative nodes (even the neutral ones) so
// switching between built-ins is deterministic — applying one fully overrides the
// previous look rather than leaving stale state behind.
namespace preset {

struct Builtin {
    QString id;       // stable identifier
    QString name;     // display name
    QString category; // "B&W" or "Color"
    QJsonObject data; // preset document (fromGraph format)
};

// The built-in preset library, ordered for display (B&W first, then Colour).
QVector<Builtin> builtins();

// The user's own presets, scanned from userPresetsDir() and tagged with the
// "My Presets" category. Ordered by display name; unreadable/invalid files are
// skipped. Re-reads the directory on each call so newly-saved presets appear.
QVector<Builtin> userPresets();

// The full browser library: built-ins followed by user presets.
QVector<Builtin> library();

// The .lumenpreset file path a user-preset id refers to, or an empty string if
// `id` is not a user preset (e.g. a built-in). Used to rename/delete from the UI.
QString userPresetPath(const QString &id);

} // namespace preset
