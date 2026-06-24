#pragma once

#include "core/Image.h"

#include <QString>
#include <QStringList>

#include <cstddef>

// Camera-RAW decoding via LibRaw. RAW files are demosaiced and colour-managed to
// an 8-bit sRGB Image (camera white balance, sRGB primaries) so they drop into
// the same pipeline as a JPEG. A 16-bit-linear path is a separate future effort.
namespace raw {

// Known camera-RAW file extensions (lowercase, no dot). Single source of truth
// for both isRawPath() and the open-file dialog filter.
const QStringList &extensions();

// True if `path`'s extension is a known camera-RAW format (case-insensitive).
bool isRawPath(const QString &path);

// Decodes a RAW file to an 8-bit sRGB Image. Null Image + *error on failure.
Image decodeFile(const QString &path, QString *error = nullptr);

// Decodes a RAW image from an in-memory buffer (the source embedded in a .lumen).
Image decodeBytes(const void *data, qsizetype size, QString *error = nullptr);

} // namespace raw
