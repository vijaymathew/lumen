#include "core/Project.h"

#include <QDataStream>
#include <QFile>
#include <QJsonDocument>

#include <cstring>

namespace {
constexpr int kMagicLen = 8;
const char kMagic[kMagicLen] = {'L', 'U', 'M', 'E', 'N', 'P', 'R', 'J'};
} // namespace

bool project::save(const QString &path, const QJsonObject &graph,
                   const QByteArray &sourceBytes, const QString &sourceName,
                   QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open '%1' for writing").arg(path);
        return false;
    }
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setVersion(QDataStream::Qt_6_0);

    s.writeRawData(kMagic, kMagicLen);
    s << static_cast<quint32>(kVersion);
    s << QJsonDocument(graph).toJson(QJsonDocument::Compact); // length-prefixed
    s << sourceName;
    s << sourceBytes;

    if (s.status() != QDataStream::Ok || file.error() != QFile::NoError) {
        if (error)
            *error = QStringLiteral("Failed writing '%1'").arg(path);
        file.close();
        file.remove(); // don't leave a half-written project
        return false;
    }
    return true;
}

bool project::load(const QString &path, Project *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open '%1'").arg(path);
        return false;
    }
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    s.setVersion(QDataStream::Qt_6_0);

    char magic[kMagicLen] = {};
    if (s.readRawData(magic, kMagicLen) != kMagicLen
        || std::memcmp(magic, kMagic, kMagicLen) != 0) {
        if (error)
            *error = QStringLiteral("'%1' is not a Lumen project").arg(path);
        return false;
    }
    quint32 version = 0;
    s >> version;
    if (version > kVersion) {
        if (error)
            *error = QStringLiteral("Project was saved by a newer version of Lumen");
        return false;
    }

    QByteArray json;
    QString sourceName;
    QByteArray sourceBytes;
    s >> json >> sourceName >> sourceBytes;
    if (s.status() != QDataStream::Ok) {
        if (error)
            *error = QStringLiteral("'%1' is corrupt or truncated").arg(path);
        return false;
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Project manifest is invalid: %1").arg(pe.errorString());
        return false;
    }

    out->graph = doc.object();
    out->sourceName = sourceName;
    out->sourceBytes = sourceBytes;
    return true;
}
