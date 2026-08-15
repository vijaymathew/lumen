#include "core/ImageStatsCache.h"

ImageStatsCache &ImageStatsCache::instance()
{
    static ImageStatsCache cache;
    return cache;
}

std::optional<imagestats::FolderStats> ImageStatsCache::get(const QString &dir) const
{
    const auto it = m_entries.constFind(dir);
    if (it == m_entries.constEnd())
        return std::nullopt;
    return it->stats;
}

bool ImageStatsCache::isStale(const QString &dir, qint64 maxAgeMs) const
{
    const auto it = m_entries.constFind(dir);
    if (it == m_entries.constEnd())
        return true;
    return it->computedAt.msecsTo(QDateTime::currentDateTime()) >= maxAgeMs;
}

void ImageStatsCache::insert(const QString &dir, const imagestats::FolderStats &stats)
{
    m_entries.insert(dir, {stats, QDateTime::currentDateTime()});
}

void ImageStatsCache::invalidate(const QString &dir)
{
    m_entries.remove(dir);
}
