#include "ui/ImageOpenDialog.h"

#include "ui/CarouselView.h"
#include "ui/ImageGridView.h"
#include "ui/ImageMetaPanel.h"
#include "ui/ImageStatisticsDialog.h"
#include "ui/ThumbnailProxyModel.h"

#include <QAction>
#include <QClipboard>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPoint>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// Thumbnails render at this longest edge (grid icons and filmstrip cells
// alike); CarouselView's big preview loads separately, at a much larger edge.
constexpr int kThumbEdge = 192;
// The size the dialog opens at (clamped to the screen for small displays).
constexpr int kDialogW = 1040;
constexpr int kDialogH = 680;
} // namespace

ImageOpenDialog::ImageOpenDialog(QWidget *parent, const QString &caption, const QString &dir,
                                 const QStringList &nameFilters)
    : QDialog(parent)
    , m_nameFilters(nameFilters)
{
    setWindowTitle(caption);
    resize(kDialogW, kDialogH);

    // --- Model chain: QFileSystemModel -> ThumbnailProxyModel (decorates image
    // files with real thumbnails, camera RAW included). Non-matching files are
    // hidden outright (NameFilterDisables false); folders always show, so the
    // dialog still browses like a normal file picker.
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setRootPath(QDir::rootPath());
    m_fsModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_fsModel->setNameFilterDisables(false);
    m_fsModel->setNameFilters(nameFilters);

    m_thumbModel = new ThumbnailProxyModel(kThumbEdge, this);
    m_thumbModel->setSourceModel(m_fsModel);
    // The folder just navigated to almost always populates asynchronously (a
    // fresh QFileSystemModel root reports 0 rows until it finishes listing);
    // catch it here rather than racing the row count at navigateTo() time.
    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this,
            [this](const QString &path) { trySelectFirst(QDir(path).absolutePath()); });

    // One selection model shared by the grid and the carousel's filmstrip, so
    // switching views never loses what was selected or current.
    m_selectionModel = new QItemSelectionModel(m_thumbModel, this);
    connect(m_selectionModel, &QItemSelectionModel::selectionChanged, this,
            &ImageOpenDialog::updateSelectionActions);

    // --- Navigation bar: up, an editable/completing path box, quick jumps, and
    // the view/statistics toggles.
    m_upButton = new QToolButton(this);
    m_upButton->setText(QStringLiteral("↑"));
    m_upButton->setToolTip(QStringLiteral("Go to parent folder"));
    connect(m_upButton, &QToolButton::clicked, this, &ImageOpenDialog::goUp);

    m_pathEdit = new QLineEdit(this);
    auto *completer = new QCompleter(m_fsModel, this);
    m_pathEdit->setCompleter(completer);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &ImageOpenDialog::onPathEdited);

    auto *homeButton = new QToolButton(this);
    homeButton->setText(QStringLiteral("Home"));
    connect(homeButton, &QToolButton::clicked, this, [this] {
        navigateTo(QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    });
    auto *picturesButton = new QToolButton(this);
    picturesButton->setText(QStringLiteral("Pictures"));
    connect(picturesButton, &QToolButton::clicked, this, [this] {
        navigateTo(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    });

    m_viewToggle = new QToolButton(this);
    m_viewToggle->setText(QStringLiteral("Carousel"));
    m_viewToggle->setToolTip(QStringLiteral("Switch between thumbnail grid and carousel view"));
    connect(m_viewToggle, &QToolButton::clicked, this, &ImageOpenDialog::toggleView);

    auto *statsButton = new QToolButton(this);
    statsButton->setText(QStringLiteral("Statistics…"));
    connect(statsButton, &QToolButton::clicked, this, &ImageOpenDialog::showStatistics);

    auto *navRow = new QHBoxLayout;
    navRow->addWidget(m_upButton);
    navRow->addWidget(m_pathEdit, 1);
    navRow->addWidget(homeButton);
    navRow->addWidget(picturesButton);
    navRow->addSpacing(12);
    navRow->addWidget(m_viewToggle);
    navRow->addWidget(statsButton);

    // --- The two browsing views, stacked; grid is shown first (thumbnails by
    // default), both reading the same shared selection.
    m_grid = new ImageGridView(this);
    m_grid->setModel(m_thumbModel);
    m_grid->setSelectionModel(m_selectionModel);
    connect(m_grid, &QListView::activated, this, &ImageOpenDialog::onActivated);
    connect(m_grid, &ImageGridView::deleteRequested, this, &ImageOpenDialog::deleteSelected);
    connect(m_grid, &ImageGridView::copyRequested, this, &ImageOpenDialog::copySelected);
    connect(m_grid, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { showViewContextMenu(m_grid, p); });
    // Keep the thumbnail queue pointed at whatever's on screen (see
    // ThumbnailProxyModel) — without this, requests are served in the order the
    // view happens to ask for decorations, not reading order.
    connect(m_grid->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        m_thumbModel->setViewportAnchor(m_grid->indexAt(QPoint(1, 1)).row());
    });

    m_carousel = new CarouselView(this);
    m_carousel->setModel(m_thumbModel);
    m_carousel->setSelectionModel(m_selectionModel);
    connect(m_carousel, &CarouselView::activated, this, &ImageOpenDialog::onActivated);
    connect(m_carousel, &CarouselView::deleteRequested, this, &ImageOpenDialog::deleteSelected);
    connect(m_carousel, &CarouselView::copyRequested, this, &ImageOpenDialog::copySelected);
    connect(m_carousel->filmstrip(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { showViewContextMenu(m_carousel->filmstrip(), p); });

    m_viewStack = new QStackedWidget(this);
    m_viewStack->addWidget(m_grid);     // index 0: default view
    m_viewStack->addWidget(m_carousel); // index 1

    m_metaPanel = new ImageMetaPanel(this);

    auto *viewRow = new QHBoxLayout;
    viewRow->setSpacing(0);
    viewRow->addWidget(m_viewStack, 1);
    viewRow->addWidget(m_metaPanel);

    // --- Bottom row: multi-select actions, then the standard Open/Cancel box.
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setText(QStringLiteral("Delete"));
    m_deleteButton->setToolTip(QStringLiteral("Move the selected images to the trash"));
    connect(m_deleteButton, &QToolButton::clicked, this, &ImageOpenDialog::deleteSelected);

    m_copyButton = new QToolButton(this);
    m_copyButton->setText(QStringLiteral("Copy"));
    m_copyButton->setToolTip(QStringLiteral("Copy the selected images to the clipboard"));
    connect(m_copyButton, &QToolButton::clicked, this, &ImageOpenDialog::copySelected);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &ImageOpenDialog::openSelected);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(m_deleteButton);
    bottomRow->addWidget(m_copyButton);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_buttons);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(navRow);
    layout->addLayout(viewRow, 1);
    layout->addLayout(bottomRow);

    updateSelectionActions();
    navigateTo(QFileInfo(dir).isDir()
                   ? dir
                   : QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    m_grid->setFocus();
}

void ImageOpenDialog::navigateTo(const QString &path)
{
    const QString clean = QDir(path).absolutePath();
    if (!QFileInfo(clean).isDir())
        return;
    m_currentDir = clean;
    m_pathEdit->setText(clean);

    const QModelIndex rootIdx = m_thumbModel->mapFromSource(m_fsModel->index(clean));
    m_grid->setRootIndex(rootIdx);
    m_carousel->setRootIndex(rootIdx);
    m_thumbModel->setViewportAnchor(0);
    m_selectionModel->clearSelection();

    // QFileSystemModel populates a freshly-rooted folder asynchronously, so the
    // row count here is usually still 0 — selecting "row 0" now would almost
    // always no-op. selectFirstOnceLoaded (wired in the constructor) lands the
    // selection once the folder actually has rows, so the carousel/meta panel
    // show something without racing the model.
    m_pendingSelectFirst = clean;
    trySelectFirst(clean); // works immediately if the folder was already loaded
}

void ImageOpenDialog::trySelectFirst(const QString &dir)
{
    if (m_pendingSelectFirst != dir)
        return;
    const QModelIndex rootIdx = m_thumbModel->mapFromSource(m_fsModel->index(dir));
    const QModelIndex first = m_thumbModel->index(0, 0, rootIdx);
    if (first.isValid()) {
        m_selectionModel->setCurrentIndex(first, QItemSelectionModel::ClearAndSelect);
        m_pendingSelectFirst.clear();
    }
}

void ImageOpenDialog::goUp()
{
    QDir d(m_currentDir);
    if (d.cdUp())
        navigateTo(d.absolutePath());
}

void ImageOpenDialog::onPathEdited()
{
    const QString path = m_pathEdit->text();
    if (QFileInfo(path).isDir())
        navigateTo(path);
    else
        m_pathEdit->setText(m_currentDir); // not a folder — revert
}

void ImageOpenDialog::toggleView()
{
    const bool showingGrid = m_viewStack->currentWidget() == m_grid;
    m_viewStack->setCurrentWidget(showingGrid ? static_cast<QWidget *>(m_carousel)
                                              : static_cast<QWidget *>(m_grid));
    m_viewToggle->setText(showingGrid ? QStringLiteral("Thumbnails")
                                      : QStringLiteral("Carousel"));
}

QStringList ImageOpenDialog::selectedImagePaths() const
{
    // selectedRows() reconstructs each index via model()->index(row, 0, parent)
    // and checks isSelected() on the result — which comes up empty here, since
    // QFileSystemModel resorts once a freshly-opened folder finishes its async
    // listing and that reconstruction doesn't land on the (now different) node
    // the selection actually tracks. selectedIndexes() returns the tracked
    // indices directly (kept valid across the resort by Qt's persistent-index
    // machinery), so it doesn't hit that gap.
    QStringList paths;
    const QModelIndexList idxs = m_selectionModel->selectedIndexes();
    paths.reserve(idxs.size());
    for (const QModelIndex &idx : idxs) {
        if (idx.column() != 0)
            continue;
        const QString path = idx.data(QFileSystemModel::FilePathRole).toString();
        if (!path.isEmpty() && QFileInfo(path).isFile())
            paths << path;
    }
    return paths;
}

void ImageOpenDialog::updateSelectionActions()
{
    const QStringList paths = selectedImagePaths();
    const bool hasSelection = !paths.isEmpty();
    m_deleteButton->setEnabled(hasSelection);
    m_copyButton->setEnabled(hasSelection);
    if (QPushButton *openBtn = m_buttons->button(QDialogButtonBox::Open))
        openBtn->setEnabled(hasSelection);
    m_metaPanel->setSelection(paths);
}

void ImageOpenDialog::deleteSelected()
{
    const QStringList paths = selectedImagePaths();
    if (paths.isEmpty())
        return;
    const QString msg =
        paths.size() == 1
            ? QStringLiteral("Move \"%1\" to the trash?").arg(QFileInfo(paths.first()).fileName())
            : QStringLiteral("Move %1 selected images to the trash?").arg(paths.size());
    if (QMessageBox::question(this, QStringLiteral("Delete images"), msg,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    int failed = 0;
    for (const QString &p : paths) {
        if (!QFile::moveToTrash(p))
            ++failed;
    }
    if (failed > 0)
        QMessageBox::warning(
            this, QStringLiteral("Delete images"),
            QStringLiteral("%1 of %2 file(s) could not be moved to the trash.")
                .arg(failed)
                .arg(paths.size()));
    // QFileSystemModel's own filesystem watcher notices the removal and updates
    // the views; nothing else to refresh here.
}

void ImageOpenDialog::copySelected()
{
    const QStringList paths = selectedImagePaths();
    if (paths.isEmpty())
        return;
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &p : paths)
        urls << QUrl::fromLocalFile(p);
    auto *data = new QMimeData;
    data->setUrls(urls);
    QGuiApplication::clipboard()->setMimeData(data);
}

void ImageOpenDialog::openSelected()
{
    const QStringList paths = selectedImagePaths();
    if (paths.isEmpty())
        return;
    m_result = paths;
    accept();
}

void ImageOpenDialog::showStatistics()
{
    ImageStatisticsDialog dlg(m_currentDir, this);
    dlg.exec();
}

void ImageOpenDialog::onActivated(const QModelIndex &index)
{
    const QString path = index.data(QFileSystemModel::FilePathRole).toString();
    if (path.isEmpty())
        return;
    if (QFileInfo(path).isDir()) {
        navigateTo(path);
        return;
    }
    // Qt selects an item before activating it (double-click, or Enter on the
    // current item), so the current selection *is* what was activated — for a
    // multi-select-then-Enter, that means opening everything selected.
    openSelected();
}

void ImageOpenDialog::showViewContextMenu(QAbstractItemView *view, const QPoint &pos)
{
    const bool hasSelection = !selectedImagePaths().isEmpty();
    QMenu menu(this);
    QAction *openAct = menu.addAction(QStringLiteral("Open"));
    openAct->setEnabled(hasSelection);
    menu.addSeparator();
    QAction *copyAct = menu.addAction(QStringLiteral("Copy"));
    copyAct->setEnabled(hasSelection);
    QAction *deleteAct = menu.addAction(QStringLiteral("Move to Trash"));
    deleteAct->setEnabled(hasSelection);

    QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
    if (chosen == openAct)
        openSelected();
    else if (chosen == copyAct)
        copySelected();
    else if (chosen == deleteAct)
        deleteSelected();
}

void ImageOpenDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_sizedOnce)
        return;
    m_sizedOnce = true;
    // Some window managers restore a window's previous geometry on map, which
    // would keep the dialog at whatever (over-wide) size it had before. Re-apply
    // a normal size now that we're mapped, clamped to the screen, and centre it.
    int w = kDialogW, h = kDialogH;
    if (const QScreen *screen = QGuiApplication::screenAt(pos())
                                    ? QGuiApplication::screenAt(pos())
                                    : QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        w = std::min(w, avail.width() - 80);
        h = std::min(h, avail.height() - 80);
        resize(w, h);
        move(avail.center() - QPoint(w / 2, h / 2));
    } else {
        resize(w, h);
    }
}

QStringList ImageOpenDialog::getOpenFileNames(QWidget *parent, const QString &caption,
                                              const QString &dir, const QStringList &nameFilters)
{
    ImageOpenDialog dlg(parent, caption, dir, nameFilters);
    if (dlg.exec() != QDialog::Accepted)
        return {};
    return dlg.m_result;
}
