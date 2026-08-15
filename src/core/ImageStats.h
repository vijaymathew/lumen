#pragma once

#include <QString>
#include <QVector>

#include <functional>

// Aggregate statistics over every supported image in a folder (RAW + the
// standard raster formats Lumen opens), used by the image picker's
// "Statistics" view. Pure, Qt-Gui-only logic — no widgets — so it's unit
// testable without a QApplication.
namespace imagestats {

// One bucket of the focal-length histogram, e.g. "24-35mm".
struct FocalBucket {
    QString label;
    int count = 0;
};

// One camera model and how many images in the scan were shot with it.
struct CameraCount {
    QString name; // "Canon EOS 5D Mark III" (maker + model, deduplicated like the info panel)
    int count = 0;
};

struct FolderStats {
    int totalImages = 0;   // every file classified as an image, RAW or raster
    int rawCount = 0;
    int rasterCount = 0;

    // Color vs monochrome. RAW files use the sensor's own colour-channel count
    // (authoritative); raster files fall back to sampling decoded pixels for
    // near-equal R/G/B (a heuristic — a "shot in b/w" JPEG from a colour sensor
    // still classifies as monochrome, which is what a viewer actually sees).
    int colorCount = 0;
    int monoCount = 0;
    int unknownColorCount = 0; // file couldn't be decoded/sampled

    // Focal length, RAW only (no EXIF reader for raster formats in this
    // codebase — see RawLoader.h). Buckets are in a fixed, meaningful order
    // (not alphabetical), parallel arrays sized/ordered together.
    QVector<FocalBucket> focalBuckets;
    int focalKnownCount = 0; // files that contributed to focalBuckets

    // Camera model breakdown (RAW only), sorted most-common first.
    QVector<CameraCount> cameras;

    int folderCount = 0; // subfolders visited (0 when not recursing)
};

// Recursively (when `recursive`) walks `rootDir`, classifying every supported
// image file. `progress`, if set, is called after each file with the running
// count of files scanned so far (for a progress dialog) — it may be called
// from whatever thread invokes computeFolderStats. `canceled`, if set, is
// polled the same way; once it returns true the scan stops and returns
// whatever it has accumulated so far (a partial, undercounted FolderStats —
// callers that care about accuracy should discard it rather than cache it).
FolderStats computeFolderStats(const QString &rootDir, bool recursive = true,
                               const std::function<void(int scanned)> &progress = {},
                               const std::function<bool()> &canceled = {});

} // namespace imagestats
