#include "core/Autosave.h"

#include "core/Project.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// Files this module owns (a saved project and its in-flight temp). Kept narrow so
// purge/scan never touch unrelated files a user might drop in the directory.
const QStringList kRecoveryGlobs = {QStringLiteral("*.lumen"),
                                    QStringLiteral("*.lumen.tmp")};

} // namespace

QString autosave::projectsDir()
{
    const QString path = QDir(QDir::homePath()).filePath(QStringLiteral(".lumen/projects"));
    if (!QDir().mkpath(path))
        return QString();
    return path;
}

void autosave::purgeOlderThan(const QString &dir, int days, const QDateTime &now)
{
    if (dir.isEmpty())
        return;
    const QDateTime cutoff = now.addDays(-days);
    const QFileInfoList entries =
        QDir(dir).entryInfoList(kRecoveryGlobs, QDir::Files, QDir::NoSort);
    for (const QFileInfo &fi : entries) {
        if (fi.lastModified() < cutoff)
            QFile::remove(fi.absoluteFilePath());
    }
}

QString autosave::newRecoveryPath(const QString &dir, const QString &sourceName,
                                  const QDateTime &now)
{
    QString base = QFileInfo(sourceName).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("untitled");
    const QString name = QStringLiteral("%1-%2-%3.lumen")
                             .arg(base, now.toString(QStringLiteral("yyyyMMdd-HHmmss")))
                             .arg(QCoreApplication::applicationPid());
    return QDir(dir).filePath(name);
}

QStringList autosave::findRecoveryFiles(const QString &dir)
{
    if (dir.isEmpty())
        return {};
    // Newest first so the caller can offer the most recent crash to restore.
    const QFileInfoList entries = QDir(dir).entryInfoList(
        {QStringLiteral("*.lumen")}, QDir::Files, QDir::Time);
    QStringList paths;
    paths.reserve(entries.size());
    for (const QFileInfo &fi : entries)
        paths << fi.absoluteFilePath();
    return paths;
}

bool autosave::writeProjectAtomic(const QString &path, const QJsonObject &graph,
                                  const QByteArray &sourceBytes,
                                  const QString &sourceName, QString *error)
{
    const QString tmp = path + QStringLiteral(".tmp");
    if (!project::save(tmp, graph, sourceBytes, sourceName, error))
        return false;
    // Rename over the destination. QFile::rename won't overwrite, so drop any
    // stale target first; on POSIX the rename itself is atomic.
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        if (error)
            *error = QStringLiteral("Could not replace '%1'").arg(path);
        QFile::remove(tmp);
        return false;
    }
    return true;
}
