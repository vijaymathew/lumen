#pragma once

#include "core/ImageStats.h"

#include <QDateTime>
#include <QHash>
#include <QString>

#include <optional>

// Process-lifetime cache of folder statistics, keyed by absolute folder path.
// Shared by every ImageOpenDialog instance for as long as Lumen runs, so
// revisiting a folder (even in a later Open dialog session) shows its
// last-known stats instantly instead of rescanning from scratch. There is
// exactly one image picker open at a time, so a plain singleton is enough —
// no need for anything more elaborate than a function-local static instance.
class ImageStatsCache {
public:
    static ImageStatsCache &instance();

    // The cached stats for `dir`, if any — regardless of age. Callers that
    // care about freshness should check isStale() too.
    std::optional<imagestats::FolderStats> get(const QString &dir) const;

    // True if there's no entry for `dir`, or its entry is older than
    // `maxAgeMs` (default 5 minutes, matching ImageOpenDialog's periodic
    // background refresh — the cache doesn't refresh itself on a timer, it
    // just reports staleness for whoever owns that timer to act on).
    bool isStale(const QString &dir, qint64 maxAgeMs = 5 * 60 * 1000) const;

    void insert(const QString &dir, const imagestats::FolderStats &stats);
    void invalidate(const QString &dir);

private:
    struct Entry {
        imagestats::FolderStats stats;
        QDateTime computedAt;
    };
    QHash<QString, Entry> m_entries;
};
