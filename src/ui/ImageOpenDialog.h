#pragma once

#include <QDialog>
#include <QModelIndex>
#include <QString>
#include <QStringList>

class CarouselView;
class ImageGridView;
class ImageMetaPanel;
class QAbstractItemView;
class QDialogButtonBox;
class QFileSystemModel;
class QItemSelectionModel;
class QLineEdit;
class QShowEvent;
class QStackedWidget;
class QToolButton;
class ThumbnailProxyModel;

// A non-native "Open image" dialog that browses like a small image library
// rather than a generic file picker:
//  - every supported format — camera RAW included — shows a real thumbnail
//    (ThumbnailProxyModel), grid view by default;
//  - a "Carousel" view (CarouselView) swaps in a single large preview with a
//    filmstrip, sharing the grid's selection so switching views keeps it;
//  - multi-select (both views) drives Delete (moves to the system trash) and
//    Copy (file URLs onto the clipboard), and multi-file Open (each opens in
//    its own tab, like dropping several files onto the window);
//  - a metadata sidebar (ImageMetaPanel) shows the current selection's info;
//  - a "Statistics" button opens a recursive folder scan (ImageStatisticsDialog).
class ImageOpenDialog : public QDialog {
    Q_OBJECT

public:
    ImageOpenDialog(QWidget *parent, const QString &caption, const QString &dir,
                    const QStringList &nameFilters);

    // Runs the dialog modally; returns the chosen paths (empty if cancelled, or
    // if the selection was empty/directories-only).
    static QStringList getOpenFileNames(QWidget *parent, const QString &caption,
                                        const QString &dir, const QStringList &nameFilters);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void navigateTo(const QString &path);
    void trySelectFirst(const QString &dir);
    void goUp();
    void toggleView();
    void updateSelectionActions();
    void deleteSelected();
    void copySelected();
    void openSelected();
    void showStatistics();
    void onActivated(const QModelIndex &index);
    void onPathEdited();
    void showViewContextMenu(QAbstractItemView *view, const QPoint &pos);

    QStringList selectedImagePaths() const; // files only, directories excluded

    QFileSystemModel *m_fsModel = nullptr;
    ThumbnailProxyModel *m_thumbModel = nullptr;
    QItemSelectionModel *m_selectionModel = nullptr;

    ImageGridView *m_grid = nullptr;
    CarouselView *m_carousel = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    ImageMetaPanel *m_metaPanel = nullptr;

    QLineEdit *m_pathEdit = nullptr;
    QToolButton *m_upButton = nullptr;
    QToolButton *m_viewToggle = nullptr;
    QToolButton *m_deleteButton = nullptr;
    QToolButton *m_copyButton = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    QString m_currentDir;
    QString m_pendingSelectFirst; // dir waiting for QFileSystemModel to populate rows
    QStringList m_nameFilters;
    QStringList m_result;
    bool m_sizedOnce = false;
};
