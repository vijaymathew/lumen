#include "ui/ImageGridView.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QStyledItemDelegate>

namespace {
constexpr int kIconEdge = 168; // large enough to read a scene at a glance

// Lays every cell out with the full icon box, whatever icon it holds right now
// — or none. With uniform item sizes the view measures a single cell, once,
// usually before its thumbnail has arrived, and applies that size to all of
// them until the next relayout. QStyledItemDelegate sizes a cell to its icon:
// a small placeholder icon makes it too small, and with no icon theme (a bare
// window manager such as IceWM) the file icon starts out empty, so the cell
// shrinks to just the file name. Either way, the thumbnails that land later
// get cropped until a resize forces the view to measure again.
class FixedIconDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        const QSize box = option->decorationSize; // the view's icon size
        QStyledItemDelegate::initStyleOption(option, index);
        // A missing icon paints nothing but still keeps its space; real icons
        // paint centred in the box with their aspect ratio kept.
        option->features |= QStyleOptionViewItem::HasDecoration;
        option->decorationSize = box;
    }
};
} // namespace

ImageGridView::ImageGridView(QWidget *parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setItemDelegate(new FixedIconDelegate(this));
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
