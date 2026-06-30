#pragma once

#include <QString>
#include <QStringList>

class QByteArray;
class QDateTime;
class QJsonObject;

// Autosave persists in-progress work so a crash doesn't lose it. The MainWindow
// drives the policy (when, where, and the recovery prompt); this namespace holds
// the UI-free filesystem helpers, kept here so they can be unit-tested without
// pulling in Qt Widgets.
//
// Recovery files live in ~/.lumen/projects. While a document has no user-chosen
// path, autosave writes a recovery .lumen there; finding one on the next launch
// means the previous session ended without the work being saved elsewhere.
namespace autosave {

// ~/.lumen/projects, created (mkpath) on first use. Empty on failure.
QString projectsDir();

// Deletes *.lumen and *.lumen.tmp files in `dir` last modified before
// `now - days`. `now` is injected so the cleanup is testable.
void purgeOlderThan(const QString &dir, int days, const QDateTime &now);

// A fresh, unique recovery path in `dir` for a session editing `sourceName`:
// "<base>-<yyyyMMdd-HHmmss>-<pid>.lumen". `now` is injected for testability.
QString newRecoveryPath(const QString &dir, const QString &sourceName,
                        const QDateTime &now);

// Recovery .lumen files in `dir`, newest (by last-modified) first.
QStringList findRecoveryFiles(const QString &dir);

// Writes a project atomically: serialises to `path + ".tmp"` via project::save,
// then renames it over `path` so a crash mid-write can never corrupt an existing
// recovery file or the user's saved document. Returns false (and sets *error) on
// failure, leaving any prior `path` intact.
bool writeProjectAtomic(const QString &path, const QJsonObject &graph,
                        const QByteArray &sourceBytes, const QString &sourceName,
                        QString *error = nullptr);

} // namespace autosave
