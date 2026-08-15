// Unit test for ImageStatsCache: get/insert/invalidate and the staleness
// check (exercised via maxAgeMs rather than a real wait — 0ms is always
// stale, a huge maxAgeMs is never stale for a just-inserted entry).

#include "core/ImageStatsCache.h"

#include <QString>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main()
{
    ImageStatsCache &cache = ImageStatsCache::instance();
    const QString dir = QStringLiteral("/some/unique/test/dir/for/imagestatscache_test");

    // Missing entries are always stale and empty.
    CHECK(!cache.get(dir).has_value());
    CHECK(cache.isStale(dir));

    imagestats::FolderStats stats;
    stats.totalImages = 7;
    stats.colorCount = 5;
    stats.monoCount = 2;
    cache.insert(dir, stats);

    const auto got = cache.get(dir);
    CHECK(got.has_value());
    CHECK(got->totalImages == 7);
    CHECK(got->colorCount == 5);
    CHECK(got->monoCount == 2);

    // Just inserted: fresh under a generous budget, stale under none at all.
    CHECK(!cache.isStale(dir, 60 * 1000));
    CHECK(cache.isStale(dir, 0));

    cache.invalidate(dir);
    CHECK(!cache.get(dir).has_value());
    CHECK(cache.isStale(dir));

    std::fprintf(stderr, "OK\n");
    return 0;
}
