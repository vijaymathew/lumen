#include "core/ImageStats.h"

#include "core/RawLoader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QSet>

#include <algorithm>
#include <cstdlib>

namespace {

// Standard raster formats Lumen opens (mirrors ThumbnailProxyModel.cpp /
// MainWindow::openImageDialog's filter — RAW extensions come from the single
// source of truth, raw::extensions()).
const QSet<QString> &rasterExtensions()
{
    static const QSet<QString> kExts = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp"),
        QStringLiteral("bmp"), QStringLiteral("gif"),
    };
    return kExts;
}

bool isSupportedImage(const QFileInfo &info)
{
    return rasterExtensions().contains(info.suffix().toLower()) || raw::isRawPath(info.filePath());
}

// A fixed, human-meaningful bucket order (not alphabetical) so the UI can
// render a histogram left-to-right by focal length. `hi < 0` = no upper bound.
struct BucketRange {
    QString label;
    float lo;
    float hi;
};

const QVector<BucketRange> &bucketRanges()
{
    static const QVector<BucketRange> kRanges = {
        {QStringLiteral("<24mm"), 0.0f, 24.0f},     {QStringLiteral("24-35mm"), 24.0f, 35.0f},
        {QStringLiteral("35-50mm"), 35.0f, 50.0f},  {QStringLiteral("50-85mm"), 50.0f, 85.0f},
        {QStringLiteral("85-135mm"), 85.0f, 135.0f}, {QStringLiteral("135mm+"), 135.0f, -1.0f},
    };
    return kRanges;
}

int bucketIndex(float focalLength)
{
    const auto &ranges = bucketRanges();
    for (int i = 0; i < ranges.size(); ++i) {
        if (focalLength >= ranges[i].lo && (ranges[i].hi < 0.0f || focalLength < ranges[i].hi))
            return i;
    }
    return ranges.size() - 1;
}

// True if a decoded image's pixels look monochrome (R≈G≈B throughout) — the
// only signal available for raster formats, which carry no EXIF reader here.
bool looksMonochrome(const QImage &img)
{
    if (img.isNull())
        return false;
    const QImage sample = img.convertToFormat(QImage::Format_RGB32)
                              .scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    int maxDelta = 0;
    for (int y = 0; y < sample.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            const QRgb px = row[x];
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);
            maxDelta = std::max({maxDelta, std::abs(r - g), std::abs(g - b), std::abs(r - b)});
        }
    }
    return maxDelta <= 6; // small tolerance for JPEG chroma noise on true b/w shots
}

// Matches MainWindow::rebuildInfo's dedup: don't repeat the maker when the
// model already states it (e.g. "NIKON" + "NIKON Z 7").
QString cameraDisplayName(const raw::LensMetadata &m)
{
    QString camera = m.cameraMaker;
    if (!m.cameraModel.isEmpty()) {
        if (camera.isEmpty() || m.cameraModel.startsWith(camera, Qt::CaseInsensitive))
            camera = m.cameraModel;
        else
            camera = camera + QLatin1Char(' ') + m.cameraModel;
    }
    return camera.trimmed();
}

} // namespace

imagestats::FolderStats imagestats::computeFolderStats(const QString &rootDir, bool recursive,
                                                        const std::function<void(int)> &progress,
                                                        const std::function<bool()> &canceled)
{
    FolderStats stats;
    stats.focalBuckets.reserve(bucketRanges().size());
    for (const BucketRange &r : bucketRanges())
        stats.focalBuckets.push_back({r.label, 0});

    QHash<QString, int> cameraTally;
    QSet<QString> subfolders;
    const QString rootAbs = QDir(rootDir).absolutePath();

    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot, flags);
    int scanned = 0;
    while (it.hasNext()) {
        if (canceled && canceled())
            break; // caller no longer wants this scan — stop burning CPU on it
        const QString path = it.next();
        const QFileInfo info(path);
        if (!isSupportedImage(info))
            continue;

        ++stats.totalImages;
        if (info.absolutePath() != rootAbs)
            subfolders.insert(info.absolutePath());

        if (raw::isRawPath(path)) {
            ++stats.rawCount;
            raw::LensMetadata meta;
            if (raw::readMetadata(path, &meta)) {
                if (meta.monochrome)
                    ++stats.monoCount;
                else
                    ++stats.colorCount;
                if (meta.focalLength > 0.0f) {
                    ++stats.focalKnownCount;
                    ++stats.focalBuckets[bucketIndex(meta.focalLength)].count;
                }
                const QString camera = cameraDisplayName(meta);
                if (!camera.isEmpty())
                    ++cameraTally[camera];
            } else {
                ++stats.unknownColorCount;
            }
        } else {
            ++stats.rasterCount;
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize full = reader.size();
            if (full.isValid())
                reader.setScaledSize(full.scaled(64, 64, Qt::KeepAspectRatio));
            const QImage img = reader.read();
            if (img.isNull())
                ++stats.unknownColorCount;
            else if (looksMonochrome(img))
                ++stats.monoCount;
            else
                ++stats.colorCount;
        }

        ++scanned;
        if (progress)
            progress(scanned);
    }

    stats.folderCount = subfolders.size();

    stats.cameras.reserve(cameraTally.size());
    for (auto i = cameraTally.constBegin(); i != cameraTally.constEnd(); ++i)
        stats.cameras.push_back({i.key(), i.value()});
    std::sort(stats.cameras.begin(), stats.cameras.end(),
              [](const CameraCount &a, const CameraCount &b) { return a.count > b.count; });

    return stats;
}
