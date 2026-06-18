#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

// A .lumen project: the serialised edit graph (EditGraph::saveState) plus the
// original source image embedded verbatim, so the file is a self-contained
// document. Binary container: an 8-byte magic, a version, then the JSON manifest,
// the source file name, and the source bytes (all length-prefixed via
// QDataStream).
namespace project {

constexpr quint32 kVersion = 1;

struct Project {
    QJsonObject graph;      // EditGraph::saveState()
    QByteArray sourceBytes; // the original encoded image (jpg/png/…), verbatim
    QString sourceName;     // original file name (for re-export naming)
};

// Writes a .lumen file. Returns false and sets *error on failure.
bool save(const QString &path, const QJsonObject &graph, const QByteArray &sourceBytes,
          const QString &sourceName, QString *error = nullptr);

// Reads a .lumen file into *out. Returns false and sets *error on failure.
bool load(const QString &path, Project *out, QString *error = nullptr);

} // namespace project
