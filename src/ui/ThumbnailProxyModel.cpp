#include "ui/ThumbnailProxyModel.h"

#include "core/RawLoader.h"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QThread>
#include <QtConcurrent>

#include <algorithm>

namespace {

// How long requests are collected before the queue is served (see schedule()).
constexpr int kCoalesceMs = 20;
// Most requests kept waiting — far more than any viewport holds (see schedule()).
constexpr int kMaxQueued = 512;
// Rank penalty that puts every row above the viewport behind every row in it.
constexpr int kAboveViewport = 1 << 20;

// Formats we render ourselves. Standard raster formats go through QImageReader;
// RAW is routed to the embedded-preview extractor (raw::isRawPath covers those).
bool isThumbnailable(const QString &path)
{
    static const QSet<QString> kStd = {
        QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("webp"),
        QStringLiteral("bmp"),  QStringLiteral("gif"),
    };
    return kStd.contains(QFileInfo(path).suffix().toLower()) || raw::isRawPath(path);
}

// Generates a thumbnail for `path`, longest edge ≤ `edge`. Runs on a worker
// thread — must touch nothing but its arguments (no QPixmap, no model state).
QImage renderThumbnail(const QString &path, int edge)
{
    if (raw::isRawPath(path))
        return raw::loadThumbnail(path, edge);

    QImageReader reader(path);
    reader.setAutoTransform(true); // honour EXIF orientation
    const QSize full = reader.size();
    if (full.isValid() && (full.width() > edge || full.height() > edge))
        reader.setScaledSize(full.scaled(edge, edge, Qt::KeepAspectRatio));
    return reader.read();
}

} // namespace

ThumbnailProxyModel::ThumbnailProxyModel(int thumbEdge, QObject *parent)
    : QIdentityProxyModel(parent)
    , m_thumbEdge(thumbEdge)
{
    m_cache.setMaxCost(1024); // cap the number of cached thumbnails
    // RAW decoding is CPU-heavy; keep a couple of cores in reserve for the UI and
    // QFileSystemModel's own gatherer thread.
    m_pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));

    m_dispatchTimer.setSingleShot(true);
    connect(&m_dispatchTimer, &QTimer::timeout, this, [this] { dispatch(); });
}

ThumbnailProxyModel::~ThumbnailProxyModel()
{
    // Let in-flight renders finish before members (the pool, the cache) go away;
    // the render tasks capture only value arguments, so this is a bounded wait.
    m_pool.clear();
    m_pool.waitForDone();
}

QVariant ThumbnailProxyModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DecorationRole && index.column() == 0) {
        if (auto *fsm = qobject_cast<QFileSystemModel *>(sourceModel())) {
            const QFileInfo info = fsm->fileInfo(mapToSource(index));
            if (info.isFile() && isThumbnailable(info.filePath())) {
                const QString path = info.filePath();
                if (QPixmap *pm = m_cache.object(path))
                    return QIcon(*pm);
                schedule(path, index);
                // Fall through to the generic icon while the thumbnail renders.
            }
        }
    }
    return QIdentityProxyModel::data(index, role);
}

void ThumbnailProxyModel::setViewportAnchor(int row)
{
    m_anchorRow = std::max(row, 0);
}

// Rank for the queue, lowest served first. Rows at or below the top of the
// viewport are what the user is looking at, so they win outright and are taken in
// reading order; rows above it (scrolled past, or asked about by the view before
// they were ever shown) rank behind all of those, nearest first.
int ThumbnailProxyModel::cost(const Request &r) const
{
    const int d = r.index.row() - m_anchorRow;
    return d >= 0 ? d : -d + kAboveViewport;
}

void ThumbnailProxyModel::schedule(const QString &path, const QModelIndex &proxyIndex) const
{
    if (m_pending.contains(path))
        return; // already queued or rendering
    m_pending.insert(path);
    m_queue.append({path, QPersistentModelIndex(proxyIndex)}); // persistent: follows sorting

    // A big folder can queue more than anyone will ever look at. Keep the best
    // candidates and drop the furthest one; anything dropped that is actually on
    // screen comes straight back, since the view re-asks for it on every repaint.
    if (m_queue.size() > kMaxQueued) {
        int worst = 0;
        for (int i = 1; i < m_queue.size(); ++i) {
            if (cost(m_queue.at(i)) > cost(m_queue.at(worst)))
                worst = i;
        }
        m_pending.remove(m_queue.takeAt(worst).path);
    }

    // Don't commit threads to the first request that happens to arrive: opening a
    // folder floods us with requests (in the view's measuring order, not reading
    // order) over a handful of event loop passes. Let that burst land, then pick
    // from the full queue. The wait is imperceptible next to a RAW decode.
    if (!m_dispatchTimer.isActive())
        m_dispatchTimer.start(kCoalesceMs);
}

void ThumbnailProxyModel::dispatch() const
{
    auto *self = const_cast<ThumbnailProxyModel *>(this);

    while (m_running < m_pool.maxThreadCount() && !m_queue.isEmpty()) {
        int best = 0;
        for (int i = 1; i < m_queue.size(); ++i) {
            if (cost(m_queue.at(i)) < cost(m_queue.at(best)))
                best = i;
        }
        const Request req = m_queue.takeAt(best);

        // The folder changed (or the file went away) while this waited its turn.
        if (!req.index.isValid()) {
            self->m_pending.remove(req.path);
            continue;
        }

        ++self->m_running;
        auto *watcher = new QFutureWatcher<QImage>(self);
        connect(watcher, &QFutureWatcher<QImage>::finished, self,
                [self, watcher, path = req.path, index = req.index] {
                    const QImage img = watcher->result();
                    --self->m_running;
                    self->m_pending.remove(path);
                    if (!img.isNull()) {
                        self->m_cache.insert(path, new QPixmap(QPixmap::fromImage(img)));
                        if (index.isValid())
                            emit self->dataChanged(index, index, {Qt::DecorationRole});
                    }
                    watcher->deleteLater();
                    self->dispatch(); // a thread just freed up
                });
        watcher->setFuture(
            QtConcurrent::run(&self->m_pool, renderThumbnail, req.path, m_thumbEdge));
    }
}
