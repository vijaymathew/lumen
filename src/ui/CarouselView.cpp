#include "ui/CarouselView.h"

#include "core/RawLoader.h"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QFutureWatcher>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListView>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
constexpr int kFilmstripIcon = 110;
// Big enough to fill the preview pane crisply on an ordinary display without
// paying for a full-sensor-resolution decode.
constexpr int kPreviewEdge = 1600;

// Loads a display-ready preview for `path`, longest edge <= `edge`. Runs on a
// worker thread (QtConcurrent) — mirrors ThumbnailProxyModel's renderThumbnail
// but at carousel size rather than grid-thumbnail size.
QImage loadPreviewImage(const QString &path, int edge)
{
    if (raw::isRawPath(path))
        return raw::loadThumbnail(path, edge);

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid() && (full.width() > edge || full.height() > edge))
        reader.setScaledSize(full.scaled(edge, edge, Qt::KeepAspectRatio));
    return reader.read();
}
} // namespace

CarouselView::CarouselView(QWidget *parent)
    : QWidget(parent)
{
    m_cache.setMaxCost(8); // a handful of full-size previews is plenty

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(200, 200);
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_preview->setStyleSheet(QStringLiteral(
        "background:#111113; border:1px solid #38383d; border-radius:8px; color:#8a8a90;"));

    m_caption = new QLabel(this);
    m_caption->setAlignment(Qt::AlignCenter);
    m_caption->setStyleSheet(QStringLiteral("color:#b4b4b8; font-size:12px;"));

    m_filmstrip = new QListView(this);
    m_filmstrip->setViewMode(QListView::IconMode);
    m_filmstrip->setFlow(QListView::LeftToRight);
    m_filmstrip->setWrapping(false);
    m_filmstrip->setResizeMode(QListView::Adjust);
    m_filmstrip->setMovement(QListView::Static);
    m_filmstrip->setUniformItemSizes(true);
    m_filmstrip->setIconSize(QSize(kFilmstripIcon, kFilmstripIcon));
    m_filmstrip->setGridSize(QSize(kFilmstripIcon + 16, kFilmstripIcon + 16));
    m_filmstrip->setSpacing(6);
    m_filmstrip->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_filmstrip->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_filmstrip->setContextMenuPolicy(Qt::CustomContextMenu);
    m_filmstrip->setFixedHeight(kFilmstripIcon + 32);
    m_filmstrip->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_filmstrip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_filmstrip, &QListView::activated, this, &CarouselView::activated);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(m_preview, 1);
    layout->addWidget(m_caption);
    layout->addWidget(m_filmstrip);

    setFocusPolicy(Qt::StrongFocus);
}

void CarouselView::setModel(QAbstractItemModel *model)
{
    m_model = model;
    m_filmstrip->setModel(model);
}

void CarouselView::setSelectionModel(QItemSelectionModel *selectionModel)
{
    m_filmstrip->setSelectionModel(selectionModel);
    connect(selectionModel, &QItemSelectionModel::currentChanged, this,
            &CarouselView::onCurrentChanged);
    onCurrentChanged(selectionModel->currentIndex(), QModelIndex());
}

void CarouselView::setRootIndex(const QModelIndex &root)
{
    m_filmstrip->setRootIndex(root);
}

void CarouselView::onCurrentChanged(const QModelIndex &current, const QModelIndex &)
{
    const QString path =
        current.isValid() ? current.data(QFileSystemModel::FilePathRole).toString() : QString();
    if (path.isEmpty() || QFileInfo(path).isDir()) {
        ++m_loadToken; // invalidate any in-flight load
        m_currentImage = QImage();
        m_currentPath.clear();
        m_preview->setPixmap(QPixmap());
        m_preview->setText(path.isEmpty() ? QStringLiteral("No image selected") : QString());
        m_caption->clear();
        return;
    }
    m_filmstrip->scrollTo(current);
    loadPreview(path);
}

void CarouselView::loadPreview(const QString &path)
{
    m_currentPath = path;
    m_caption->setText(QFileInfo(path).fileName());

    if (QImage *cached = m_cache.object(path)) {
        applyPreview(*cached);
        return;
    }

    m_preview->setPixmap(QPixmap());
    m_preview->setText(QStringLiteral("Loading…"));
    const qint64 token = ++m_loadToken;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, path, token] {
        const QImage img = watcher->result();
        watcher->deleteLater();
        if (token != m_loadToken || path != m_currentPath)
            return; // superseded by a newer selection
        if (!img.isNull())
            m_cache.insert(path, new QImage(img));
        applyPreview(img);
    });
    watcher->setFuture(QtConcurrent::run(loadPreviewImage, path, kPreviewEdge));
}

void CarouselView::applyPreview(const QImage &img)
{
    m_currentImage = img;
    if (img.isNull()) {
        m_preview->setText(QStringLiteral("No preview available"));
        return;
    }
    rescalePreview();
}

void CarouselView::rescalePreview()
{
    if (m_currentImage.isNull())
        return;
    const QPixmap pm = QPixmap::fromImage(m_currentImage)
                            .scaled(m_preview->size(), Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    m_preview->setPixmap(pm);
}

void CarouselView::keyPressEvent(QKeyEvent *event)
{
    QItemSelectionModel *sel = m_filmstrip->selectionModel();
    if (sel && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const QModelIndex cur = sel->currentIndex();
        const int row = cur.row() + (event->key() == Qt::Key_Right ? 1 : -1);
        const QModelIndex next = m_model ? m_model->index(row, 0, cur.parent()) : QModelIndex();
        if (next.isValid()) {
            sel->setCurrentIndex(next, QItemSelectionModel::ClearAndSelect);
            m_filmstrip->scrollTo(next);
        }
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deleteRequested();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        emit copyRequested();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (sel && sel->currentIndex().isValid())
            emit activated(sel->currentIndex());
        return;
    }
    QWidget::keyPressEvent(event);
}

void CarouselView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rescalePreview();
}
