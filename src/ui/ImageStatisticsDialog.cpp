#include "ui/ImageStatisticsDialog.h"

#include "core/ImageStatsCache.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

ImageStatisticsDialog::ImageStatisticsDialog(const QString &rootDir, QWidget *parent)
    : QDialog(parent)
    , m_rootDir(rootDir)
{
    setWindowTitle(QStringLiteral("Folder statistics"));
    resize(440, 560);

    const QString name = QFileInfo(rootDir).fileName();
    auto *folderLabel =
        new QLabel(QStringLiteral("<b>%1</b>").arg(name.isEmpty() ? rootDir : name), this);
    folderLabel->setToolTip(rootDir);
    folderLabel->setTextFormat(Qt::RichText);

    // Off by default: a plain listing of just this folder is effectively
    // instant, where a recursive scan of a large library isn't — checking
    // this is an explicit "yes, go scan more" ask.
    m_recurseCheck = new QCheckBox(QStringLiteral("Include subfolders"), this);
    m_recurseCheck->setChecked(false);
    connect(m_recurseCheck, &QCheckBox::toggled, this, &ImageStatisticsDialog::startScan);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(folderLabel, 1);
    topRow->addWidget(m_recurseCheck);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("color:#8a8a90; font-size:12px;"));

    m_resultsHost = new QWidget;
    m_resultsLayout = new QVBoxLayout(m_resultsHost);
    m_resultsLayout->setSpacing(10);
    m_resultsLayout->setContentsMargins(2, 2, 2, 2);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_resultsHost);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_summary);
    layout->addWidget(scroll, 1);
    layout->addWidget(buttons);

    connect(&m_watcher, &QFutureWatcher<imagestats::FolderStats>::finished, this,
            [this] { showResults(m_watcher.result()); });

    startScan();
}

void ImageStatisticsDialog::clearResults()
{
    QLayoutItem *item;
    while ((item = m_resultsLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
}

void ImageStatisticsDialog::startScan()
{
    const bool recursive = m_recurseCheck->isChecked();

    // Checking "include subfolders" asks for exactly the scan ImageOpenDialog
    // precomputes in the background while the user browses — if it's already
    // landed (and isn't stale), use it instead of scanning again.
    if (recursive && !ImageStatsCache::instance().isStale(m_rootDir)) {
        if (const auto cached = ImageStatsCache::instance().get(m_rootDir)) {
            showResults(*cached);
            return;
        }
    }

    m_summary->setText(QStringLiteral("Scanning…"));
    clearResults();

    const QString root = m_rootDir;
    m_watcher.setFuture(
        QtConcurrent::run([root, recursive] { return imagestats::computeFolderStats(root, recursive); }));
}

void ImageStatisticsDialog::addSectionLabel(const QString &text)
{
    auto *lbl = new QLabel(text, m_resultsHost);
    lbl->setStyleSheet(
        QStringLiteral("color:#e2e2e5; font-size:13px; font-weight:600; margin-top:6px;"));
    m_resultsLayout->addWidget(lbl);
}

void ImageStatisticsDialog::addBar(const QString &label, int value, int max)
{
    auto *row = new QWidget(m_resultsHost);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    auto *lbl = new QLabel(label, row);
    lbl->setFixedWidth(90);
    lbl->setStyleSheet(QStringLiteral("color:#c8c8cc; font-size:12px;"));

    auto *bar = new QProgressBar(row);
    bar->setRange(0, std::max(max, 1));
    bar->setValue(value);
    bar->setFormat(QStringLiteral("%v (%p%)"));
    bar->setTextVisible(true);

    h->addWidget(lbl);
    h->addWidget(bar, 1);
    m_resultsLayout->addWidget(row);
}

void ImageStatisticsDialog::showResults(const imagestats::FolderStats &s)
{
    clearResults();

    if (s.totalImages == 0) {
        m_summary->setText(QStringLiteral("No images found."));
        m_resultsLayout->addStretch(1);
        return;
    }

    QString summary =
        QStringLiteral("%1 image%2").arg(s.totalImages).arg(s.totalImages == 1 ? QString() : QStringLiteral("s"));
    if (s.folderCount > 0)
        summary += QStringLiteral(" across %1 subfolder%2")
                       .arg(s.folderCount)
                       .arg(s.folderCount == 1 ? QString() : QStringLiteral("s"));
    summary += QStringLiteral("  •  %1 RAW, %2 other").arg(s.rawCount).arg(s.rasterCount);
    if (s.unknownColorCount > 0)
        summary += QStringLiteral("  •  %1 couldn't be read").arg(s.unknownColorCount);
    m_summary->setText(summary);

    const int classified = s.colorCount + s.monoCount;
    if (classified > 0) {
        addSectionLabel(QStringLiteral("Color vs. monochrome"));
        addBar(QStringLiteral("Color"), s.colorCount, classified);
        addBar(QStringLiteral("Monochrome"), s.monoCount, classified);
    }

    if (s.rawCount > 0) {
        addSectionLabel(s.focalKnownCount > 0
                            ? QStringLiteral("Focal length — %1 of %2 RAW files")
                                  .arg(s.focalKnownCount)
                                  .arg(s.rawCount)
                            : QStringLiteral("Focal length"));
        if (s.focalKnownCount > 0) {
            for (const auto &b : s.focalBuckets)
                addBar(b.label, b.count, s.focalKnownCount);
        } else {
            auto *none = new QLabel(QStringLiteral("No focal length data available"), m_resultsHost);
            none->setStyleSheet(QStringLiteral("color:#8a8a90; font-size:12px;"));
            m_resultsLayout->addWidget(none);
        }

        if (!s.cameras.isEmpty()) {
            addSectionLabel(QStringLiteral("Cameras"));
            int cameraTotal = 0;
            for (const auto &c : s.cameras)
                cameraTotal += c.count;
            const int shown = std::min<int>(s.cameras.size(), 8);
            for (int i = 0; i < shown; ++i)
                addBar(s.cameras[i].name, s.cameras[i].count, cameraTotal);
        }
    } else {
        auto *note = new QLabel(
            QStringLiteral("Focal length and camera breakdowns need RAW files — this build has no "
                          "EXIF reader for JPEG/PNG."),
            m_resultsHost);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color:#8a8a90; font-size:12px;"));
        m_resultsLayout->addWidget(note);
    }

    m_resultsLayout->addStretch(1);
}
