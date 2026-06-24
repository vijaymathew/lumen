#pragma once

#include "core/Image.h"

#include <QString>
#include <QStringList>

#include <cstddef>

// Camera-RAW decoding via LibRaw. RAW files are demosaiced and colour-managed to
// a 16-bit sRGB Image (camera white balance, sRGB primaries) promoted to the
// float working format, so they drop into the same pipeline as a JPEG.
namespace raw {

// Lens/camera identity extracted from the RAW's EXIF, used to look up a Lensfun
// profile for automatic lens correction. Empty/zero fields mean "unknown".
struct LensMetadata {
    QString cameraMaker;     // e.g. "Canon"
    QString cameraModel;     // e.g. "EOS 5D Mark III"
    QString lensModel;       // e.g. "EF24-70mm f/2.8L II USM"
    float focalLength = 0;   // mm
    float aperture = 0;      // f-number at capture
    float focusDistance = 0; // metres (0 = unknown)
};

// Known camera-RAW file extensions (lowercase, no dot). Single source of truth
// for both isRawPath() and the open-file dialog filter.
const QStringList &extensions();

// True if `path`'s extension is a known camera-RAW format (case-insensitive).
bool isRawPath(const QString &path);

// Decodes a RAW file to a 16-bit sRGB Image. Null Image + *error on failure. When
// `meta` is non-null it receives the camera/lens identity from EXIF.
Image decodeFile(const QString &path, QString *error = nullptr, LensMetadata *meta = nullptr);

// Decodes a RAW image from an in-memory buffer (the source embedded in a .lumen).
Image decodeBytes(const void *data, qsizetype size, QString *error = nullptr,
                  LensMetadata *meta = nullptr);

} // namespace raw
