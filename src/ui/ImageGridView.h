#pragma once

#include <QListView>

// The default view of ImageOpenDialog: a large-thumbnail icon grid over
// ThumbnailProxyModel, with extended multi-select. Delete/Copy are handled
// here (not as window-wide shortcuts) so they don't fire while the dialog's
// path bar has focus.
class ImageGridView : public QListView {
    Q_OBJECT

public:
    explicit ImageGridView(QWidget *parent = nullptr);

signals:
    void deleteRequested();
    void copyRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
};
