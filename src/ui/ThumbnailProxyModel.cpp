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

void ThumbnailProxyModel::schedule(const QString &path, const QModelIndex &proxyIndex) const
{
    if (m_pending.contains(path))
        return; // already rendering
    m_pending.insert(path);

    auto *self = const_cast<ThumbnailProxyModel *>(this);
    const QPersistentModelIndex persistent(proxyIndex); // follows sorting/navigation

    auto *watcher = new QFutureWatcher<QImage>(self);
    connect(watcher, &QFutureWatcher<QImage>::finished, self,
            [self, watcher, path, persistent] {
                const QImage img = watcher->result();
                self->m_pending.remove(path);
                if (!img.isNull()) {
                    self->m_cache.insert(path, new QPixmap(QPixmap::fromImage(img)));
                    if (persistent.isValid())
                        emit self->dataChanged(persistent, persistent, {Qt::DecorationRole});
                }
                watcher->deleteLater();
            });
    watcher->setFuture(QtConcurrent::run(&self->m_pool, renderThumbnail, path, m_thumbEdge));
}
