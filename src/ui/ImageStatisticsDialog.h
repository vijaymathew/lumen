#pragma once

#include "core/ImageStats.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QString>

#include <optional>

class QCheckBox;
class QLabel;
class QVBoxLayout;

// "Statistics" for the currently browsed folder in ImageOpenDialog: a
// recursive-by-default scan (see imagestats::computeFolderStats) rendered as
// simple percentage bars — focal length spread, color vs. monochrome split,
// and a camera breakdown. The scan runs off the UI thread; toggling "include
// subfolders" re-scans.
class ImageStatisticsDialog : public QDialog {
    Q_OBJECT

public:
    // `precomputed`, if set, is shown immediately instead of scanning —
    // ImageOpenDialog precomputes the recursive scan in the background while
    // the user browses, so opening this dialog is usually instant. Only valid
    // for the recursive case (the checkbox's default); unchecking it always
    // triggers a fresh scan.
    ImageStatisticsDialog(const QString &rootDir, QWidget *parent = nullptr,
                          const std::optional<imagestats::FolderStats> &precomputed = std::nullopt);

private:
    void startScan();
    void showResults(const imagestats::FolderStats &stats);
    void addBar(const QString &label, int value, int max);
    void addSectionLabel(const QString &text);
    void clearResults();

    QString m_rootDir;
    QCheckBox *m_recurseCheck = nullptr;
    QLabel *m_summary = nullptr;
    QWidget *m_resultsHost = nullptr;
    QVBoxLayout *m_resultsLayout = nullptr;
    QFutureWatcher<imagestats::FolderStats> m_watcher;
};
