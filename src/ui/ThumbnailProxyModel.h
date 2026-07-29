#pragma once

#include <QCache>
#include <QIdentityProxyModel>
#include <QList>
#include <QPersistentModelIndex>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>

class QPixmap;

// A proxy over QFileDialog's QFileSystemModel that decorates image files — camera
// RAW included — with real thumbnails, so the picker browses like an image
// library rather than a list of generic icons. Thumbnails are generated on a
// background thread pool and cached; data() hands back the generic icon
// immediately and emits dataChanged once the picture is ready, so scrolling a
// folder full of RAWs never blocks the UI. Install with
// QFileDialog::setProxyModel() (the dialog takes ownership).
//
// Requests are queued rather than started on arrival, and the queue is served
// nearest-the-viewport first (see setViewportAnchor). The view asks for
// decorations in an order that has nothing to do with what the user is looking
// at — measuring row heights walks the *last* rows of the folder backwards, and
// measuring the name column walks every row — so serving requests as they come
// in renders the bottom of the folder before the top.
class ThumbnailProxyModel : public QIdentityProxyModel {
    Q_OBJECT

public:
    // `thumbEdge` is the longest edge (px) thumbnails are generated at.
    explicit ThumbnailProxyModel(int thumbEdge, QObject *parent = nullptr);
    ~ThumbnailProxyModel() override;

    QVariant data(const QModelIndex &index, int role) const override;

    // Tells the queue which row sits at the top of the viewport, so the visible
    // thumbnails are rendered first and in reading order. Call it whenever the
    // view scrolls; row 0 (the default) is right for a freshly opened folder.
    void setViewportAnchor(int row);

private:
    // One queued render: the file, and where to refresh when it lands.
    struct Request {
        QString path;
        QPersistentModelIndex index;
    };

    // Queue rank for a request, lowest served first: distance from the viewport,
    // with everything above it pushed behind everything in and below it.
    int cost(const Request &r) const;
    // Queues (once) a background thumbnail render for `path`, refreshing
    // `proxyIndex` when it lands. Const because it is driven from data().
    void schedule(const QString &path, const QModelIndex &proxyIndex) const;
    // Starts queued renders, closest to the viewport first, until the pool is
    // full. Called when work arrives and whenever a render finishes.
    void dispatch() const;

    const int m_thumbEdge;
    mutable QCache<QString, QPixmap> m_cache;  // path → ready thumbnail
    mutable QSet<QString> m_pending;           // queued or rendering (deduped)
    mutable QList<Request> m_queue;            // waiting for a pool thread
    mutable int m_running = 0;                 // renders currently on the pool
    mutable QTimer m_dispatchTimer;            // collects a burst of requests before serving
    int m_anchorRow = 0;                       // top of the viewport
    mutable QThreadPool m_pool;                // bounded so RAW decodes don't swamp the CPU
};
