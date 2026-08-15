#pragma once

#include "core/ImageStats.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QString>

class QCheckBox;
class QLabel;
class QVBoxLayout;

// "Statistics" for the currently browsed folder in ImageOpenDialog: a scan
// (see imagestats::computeFolderStats) rendered as simple percentage bars —
// focal length spread, color vs. monochrome split, and a camera breakdown.
// "Include subfolders" starts unchecked (a plain listing of the current
// folder is instant; a recursive scan of a large library isn't); checking it
// consults ImageStatsCache first, so if ImageOpenDialog already precomputed
// the recursive scan for this folder in the background, the result is
// usually instant there too. The scan runs off the UI thread either way.
class ImageStatisticsDialog : public QDialog {
    Q_OBJECT

public:
    ImageStatisticsDialog(const QString &rootDir, QWidget *parent = nullptr);

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
