#include "ui/ImageOpenDialog.h"

#include "ui/ThumbnailProxyModel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QListView>
#include <QPoint>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QSize>
#include <QSizePolicy>
#include <QTreeView>
#include <QVector>

#include <algorithm>

namespace {
// Thumbnails render at this longest edge and display a little smaller (QIcon
// scales the cached pixmap down, keeping it crisp).
constexpr int kThumbEdge = 192;
constexpr int kRowIcon = 96;
// The size the dialog opens at (clamped to the screen for small displays).
constexpr int kDialogW = 900;
constexpr int kDialogH = 600;
} // namespace

ImageOpenDialog::ImageOpenDialog(QWidget *parent, const QString &caption,
                                 const QString &dir, const QString &filter)
    : QFileDialog(parent, caption, dir, filter)
{
    // Thumbnails need Qt's own dialog (the native one exposes no model/views to
    // extend), and we only ever open one existing file.
    setOption(QFileDialog::DontUseNativeDialog, true);
    // Our filter lists every extension (plus uppercase variants); showing that
    // whole string in the "Files of type" combo stretches the dialog absurdly
    // wide. Hide the pattern details so the combo just reads "Images".
    setOption(QFileDialog::HideNameFilterDetails, true);
    setFileMode(QFileDialog::ExistingFile);
    setAcceptMode(QFileDialog::AcceptOpen);
    resize(kDialogW, kDialogH); // a sensible default the long filter no longer distorts

    // Decorate image files with real thumbnails. The dialog wires our proxy on top
    // of its QFileSystemModel and takes ownership.
    auto *thumbs = new ThumbnailProxyModel(kThumbEdge);
    setProxyModel(thumbs);

    // A detail (row) view: each row shows a thumbnail beside the name/size/date —
    // a clean, familiar layout that always shows file names and never leaves a
    // large empty grid. Both internal views get the larger icon size so toggling
    // list/detail with the dialog's own buttons keeps the thumbnails.
    setViewMode(QFileDialog::Detail);
    const QSize iconSize(kRowIcon, kRowIcon);
    QVector<QAbstractItemView *> views;
    if (auto *tree = findChild<QTreeView *>(QStringLiteral("treeView")))
        views << tree;
    if (auto *list = findChild<QListView *>(QStringLiteral("listView")))
        views << list;
    for (QAbstractItemView *view : views) {
        view->setIconSize(iconSize);
        // Keep the thumbnail queue pointed at whatever the user is looking at, so
        // the visible files render first and top-down. Without this the queue is
        // served in the order the view happens to ask for decorations, which
        // starts at the *bottom* of the folder (Qt measures the last rows first
        // to size the scrollbar).
        connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, [thumbs, view] {
            thumbs->setViewportAnchor(view->indexAt(QPoint(1, 1)).row());
        });
    }

    // Stop the "Look in" path combo and the filename edit from dictating the
    // dialog's minimum width. With some fonts/styles their content-derived
    // minimum grows to fit the full path, which becomes a floor the window can't
    // shrink below (so it opens screen-wide and can't be dragged smaller). Let
    // them shrink and elide instead; the layout still gives them ample space.
    for (QComboBox *combo : findChildren<QComboBox *>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(10);
        QSizePolicy sp = combo->sizePolicy();
        sp.setHorizontalPolicy(QSizePolicy::Ignored); // content width no longer a floor
        combo->setSizePolicy(sp);
        combo->setMinimumWidth(90); // stay usable, but small
    }
    if (auto *nameEdit = findChild<QLineEdit *>(QStringLiteral("fileNameEdit")))
        nameEdit->setMinimumWidth(90);
}

void ImageOpenDialog::showEvent(QShowEvent *event)
{
    QFileDialog::showEvent(event);
    if (m_sizedOnce)
        return;
    m_sizedOnce = true;
    // Some window managers restore a window's previous geometry on map, which
    // would keep the dialog at whatever (over-wide) size it had before. Re-apply a
    // normal size now that we're mapped, clamped to the screen, and centre it.
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

QString ImageOpenDialog::getOpenFileName(QWidget *parent, const QString &caption,
                                         const QString &dir, const QString &filter)
{
    ImageOpenDialog dlg(parent, caption, dir, filter);
    if (dlg.exec() != QDialog::Accepted)
        return QString();
    const QStringList selected = dlg.selectedFiles();
    return selected.isEmpty() ? QString() : selected.first();
}
