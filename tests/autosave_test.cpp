// Unit tests for the autosave filesystem helpers (core/Autosave): stale-file
// purging respects the 7-day window and file type, recovery paths are unique and
// well-formed, recovery scanning returns newest-first, and the atomic write
// round-trips a project without leaving a .tmp behind.

#include "core/Autosave.h"
#include "core/Project.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace {

bool touch(const QString &path, const QByteArray &data = "x")
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return true;
}

bool setMTime(const QString &path, const QDateTime &when)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    return f.setFileTime(when, QFileDevice::FileModificationTime);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv); // applicationPid() / Qt plumbing

    const QDateTime now = QDateTime::currentDateTime();

    // 1. purgeOlderThan: removes only *.lumen/*.tmp older than the window; keeps
    //    recent ones and anything that isn't a recovery file.
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QDir d(tmp.path());
        const QString oldProj = d.filePath(QStringLiteral("old.lumen"));
        const QString oldTmp = d.filePath(QStringLiteral("old.lumen.tmp"));
        const QString fresh = d.filePath(QStringLiteral("fresh.lumen"));
        const QString other = d.filePath(QStringLiteral("notes.txt"));
        CHECK(touch(oldProj) && touch(oldTmp) && touch(fresh) && touch(other));
        CHECK(setMTime(oldProj, now.addDays(-10)));
        CHECK(setMTime(oldTmp, now.addDays(-10)));
        CHECK(setMTime(other, now.addDays(-30))); // old, but not ours

        autosave::purgeOlderThan(tmp.path(), 7, now);

        CHECK(!QFile::exists(oldProj)); // 10 days old → removed
        CHECK(!QFile::exists(oldTmp));  // temp also swept
        CHECK(QFile::exists(fresh));    // recent → kept
        CHECK(QFile::exists(other));    // not a recovery file → untouched
    }

    // 2. newRecoveryPath: lands in the dir, ends in .lumen, carries the source
    //    base name, and differs across timestamps (uniqueness per session).
    {
        const QString dir = QStringLiteral("/tmp/lumen-x");
        const QString a =
            autosave::newRecoveryPath(dir, QStringLiteral("DSC_001.NEF"), now);
        const QString b = autosave::newRecoveryPath(dir, QStringLiteral("DSC_001.NEF"),
                                                     now.addSecs(1));
        CHECK(a.startsWith(dir + QStringLiteral("/")));
        CHECK(a.endsWith(QStringLiteral(".lumen")));
        CHECK(a.contains(QStringLiteral("DSC_001")));
        CHECK(a != b);
        // Empty source name still yields a usable path.
        const QString c = autosave::newRecoveryPath(dir, QString(), now);
        CHECK(c.endsWith(QStringLiteral(".lumen")));
    }

    // 3. findRecoveryFiles: only *.lumen, newest first.
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QDir d(tmp.path());
        const QString a = d.filePath(QStringLiteral("a.lumen"));
        const QString b = d.filePath(QStringLiteral("b.lumen"));
        const QString c = d.filePath(QStringLiteral("c.lumen"));
        CHECK(touch(a) && touch(b) && touch(c));
        CHECK(touch(d.filePath(QStringLiteral("ignore.txt"))));
        CHECK(setMTime(a, now.addSecs(-30)));
        CHECK(setMTime(b, now.addSecs(-20)));
        CHECK(setMTime(c, now.addSecs(-10))); // c newest

        const QStringList found = autosave::findRecoveryFiles(tmp.path());
        CHECK(found.size() == 3); // the .txt is excluded
        CHECK(QFileInfo(found[0]).fileName() == QStringLiteral("c.lumen"));
        CHECK(QFileInfo(found[1]).fileName() == QStringLiteral("b.lumen"));
        CHECK(QFileInfo(found[2]).fileName() == QStringLiteral("a.lumen"));
    }

    // 4. writeProjectAtomic: round-trips, and leaves no .tmp behind.
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString path = QDir(tmp.path()).filePath(QStringLiteral("p.lumen"));
        QJsonObject graph;
        graph[QStringLiteral("hello")] = 42;
        const QByteArray src = QByteArray("source-bytes");
        QString err;
        CHECK(autosave::writeProjectAtomic(path, graph, src,
                                           QStringLiteral("img.jpg"), &err));
        CHECK(QFile::exists(path));
        CHECK(!QFile::exists(path + QStringLiteral(".tmp"))); // temp cleaned up

        project::Project back;
        CHECK(project::load(path, &back));
        CHECK(back.graph.value(QStringLiteral("hello")).toInt() == 42);
        CHECK(back.sourceName == QStringLiteral("img.jpg"));
        CHECK(back.sourceBytes == src);

        // Overwriting an existing project succeeds atomically.
        graph[QStringLiteral("hello")] = 7;
        CHECK(autosave::writeProjectAtomic(path, graph, src,
                                           QStringLiteral("img.jpg"), &err));
        CHECK(project::load(path, &back));
        CHECK(back.graph.value(QStringLiteral("hello")).toInt() == 7);
    }

    std::printf("autosave_test OK\n");
    return 0;
}
