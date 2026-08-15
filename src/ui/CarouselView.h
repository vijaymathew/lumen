#pragma once

#include <QCache>
#include <QImage>
#include <QModelIndex>
#include <QString>
#include <QWidget>

class QAbstractItemModel;
class QItemSelectionModel;
class QLabel;
class QListView;

// The "Carousel" view of ImageOpenDialog: one large preview of the current
// image plus a horizontal filmstrip of thumbnails. The filmstrip shares its
// model and QItemSelectionModel with ImageGridView (set via setModel /
// setSelectionModel), so switching views keeps whatever was selected and
// current — only the presentation changes. Multi-select happens by
// ctrl/shift-clicking the filmstrip; the large pane always shows the
// selection model's *current* item.
class CarouselView : public QWidget {
    Q_OBJECT

public:
    explicit CarouselView(QWidget *parent = nullptr);

    void setModel(QAbstractItemModel *model);
    void setSelectionModel(QItemSelectionModel *selectionModel);
    void setRootIndex(const QModelIndex &root);

    QListView *filmstrip() const { return m_filmstrip; }

signals:
    void deleteRequested();
    void copyRequested();
    void activated(const QModelIndex &index); // double-click / Enter on the filmstrip

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void onCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
    void loadPreview(const QString &path);
    void applyPreview(const QImage &img);
    void rescalePreview();

    QLabel *m_preview = nullptr;
    QLabel *m_caption = nullptr;
    QListView *m_filmstrip = nullptr;

    QAbstractItemModel *m_model = nullptr;
    QString m_currentPath;
    QImage m_currentImage; // the loaded (not yet label-scaled) preview
    QCache<QString, QImage> m_cache; // avoids reloading recently-viewed previews
    qint64 m_loadToken = 0; // guards against a superseded async load landing late
};
