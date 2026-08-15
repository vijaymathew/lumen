#pragma once

#include "core/ImageStats.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QModelIndex>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

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
class QTimer;
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
//  - a "Statistics" button opens a folder scan (ImageStatisticsDialog); its
//    "include subfolders" option is off by default, but the recursive scan it
//    would run is precomputed in the background regardless (see
//    schedulePrecomputeStats) after the user has sat in a folder a few
//    seconds, so checking that box usually still shows an instant answer. The
//    result lives in ImageStatsCache for as long as Lumen runs, and is
//    refreshed on a timer while its folder stays open (see
//    kStatsRefreshIntervalMs) so it doesn't drift far from the real filesystem.
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

    // Background folder-statistics precompute (see .cpp for the full story):
    // schedulePrecomputeStats() debounces navigation into startPrecomputeStats(),
    // which runs the recursive scan on a worker thread and stores the result in
    // ImageStatsCache so showStatistics() can usually skip straight to the
    // cached answer. `force` bypasses the "already fresh" check — used by the
    // periodic refresh timer to re-scan the folder currently open regardless.
    void schedulePrecomputeStats();
    void startPrecomputeStats(bool force = false);

    QStringList selectedImagePaths() const; // files only, directories excluded
    // The file to land the selection on after deleting `deletedPaths`: the
    // next surviving image past the last (highest-row) one being deleted, or
    // the nearest surviving one before it if the deletion reached the end.
    QString pathAfterDeletion(const QStringList &deletedPaths) const;

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

    // Background statistics precompute — the cache itself (ImageStatsCache) is
    // a process-lifetime singleton shared across dialog instances; these are
    // just this dialog's handle on whatever scan it currently has in flight.
    QTimer *m_statsPrecomputeTimer = nullptr; // debounces navigation before scanning
    QTimer *m_statsRefreshTimer = nullptr;    // periodically re-scans the open folder
    QFutureWatcher<imagestats::FolderStats> m_statsWatcher;
    QString m_statsPendingDir;                           // dir the in-flight scan is for
    std::shared_ptr<std::atomic_bool> m_statsCancelFlag; // tells that scan to stop early
};
