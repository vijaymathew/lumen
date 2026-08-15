#include "ui/ImageGridView.h"

#include <QKeyEvent>
#include <QKeySequence>

namespace {
constexpr int kIconEdge = 168; // large enough to read a scene at a glance
}

ImageGridView::ImageGridView(QWidget *parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setIconSize(QSize(kIconEdge, kIconEdge));
    setGridSize(QSize(kIconEdge + 24, kIconEdge + 46));
    setSpacing(8);
    setWordWrap(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void ImageGridView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit deleteRequested();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        emit copyRequested();
        return;
    }
    QListView::keyPressEvent(event);
}
