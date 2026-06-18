#pragma once

#include <QCache>
#include <QIdentityProxyModel>
#include <QSet>
#include <QString>
#include <QThreadPool>

class QPixmap;

// A proxy over QFileDialog's QFileSystemModel that decorates image files — camera
// RAW included — with real thumbnails, so the picker browses like an image
// library rather than a list of generic icons. Thumbnails are generated on a
// background thread pool and cached; data() hands back the generic icon
// immediately and emits dataChanged once the picture is ready, so scrolling a
// folder full of RAWs never blocks the UI. Install with
// QFileDialog::setProxyModel() (the dialog takes ownership).
class ThumbnailProxyModel : public QIdentityProxyModel {
    Q_OBJECT

public:
    // `thumbEdge` is the longest edge (px) thumbnails are generated at.
    explicit ThumbnailProxyModel(int thumbEdge, QObject *parent = nullptr);
    ~ThumbnailProxyModel() override;

    QVariant data(const QModelIndex &index, int role) const override;

private:
    // Kicks off (once) a background thumbnail render for `path`, refreshing
    // `proxyIndex` when it lands. Const because it is driven from data().
    void schedule(const QString &path, const QModelIndex &proxyIndex) const;

    const int m_thumbEdge;
    mutable QCache<QString, QPixmap> m_cache;  // path → ready thumbnail
    mutable QSet<QString> m_pending;           // renders in flight (deduped)
    mutable QThreadPool m_pool;                // bounded so RAW decodes don't swamp the CPU
};
