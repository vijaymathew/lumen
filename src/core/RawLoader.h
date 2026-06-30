#pragma once

#include "core/Image.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstddef>

// Camera-RAW decoding via LibRaw. RAW files are demosaiced and colour-managed to
// a 16-bit sRGB Image (camera white balance, sRGB primaries) promoted to the
// float working format, so they drop into the same pipeline as a JPEG.
namespace raw {

// The automatic adjustments LibRaw bakes in at decode time. Defaults reproduce
// Lumen's historical behaviour exactly (auto-bright on, clip highlights, as-shot
// camera WB, AHD demosaic), so a default-constructed value is a no-op change.
// These are baked into the pixels, so they are persisted per-project (.lumen).
struct RawDecodeOptions {
    enum Wb { Camera = 0, Auto = 1, None = 2 }; // as-shot / auto-grey-world / neutral

    bool autoBright = true;           // LibRaw no_auto_bright = !autoBright
    float autoBrightThreshold = 0.01f; // auto_bright_thr (fraction clipped to white)
    int highlight = 0;                // LibRaw `highlight`: 0 clip, 2 blend, 3 reconstruct
    int wb = Camera;                  // white-balance source
    int demosaic = 3;                 // user_qual: 0 linear,1 VNG,2 PPG,3 AHD,4 DCB

    friend bool operator==(const RawDecodeOptions &, const RawDecodeOptions &) = default;

    QJsonObject toJson() const;
    static RawDecodeOptions fromJson(const QJsonObject &);
};

// Default state of the (Lensfun) automatic lens corrections seeded onto a freshly
// opened RAW's LensCorrectionNode. A global preference only — the lens node itself
// persists per-project, so these aren't stored in the .lumen.
struct RawLensDefaults {
    bool distortion = true;
    bool tca = true;
    bool vignetting = true;

    friend bool operator==(const RawLensDefaults &, const RawLensDefaults &) = default;
};

// Camera colour profile pulled from LibRaw, used by white balance (WB v2) to do a
// camera-accurate, linear-light Kelvin WB. All matrices are row-major 3x3.
// `valid` is false for files without a known colour profile (WB then falls back
// to the sRGB model).
struct ColorProfile {
    bool valid = false;
    double camToRgb[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // camera → linear sRGB (rgb_cam)
    double xyzToCam[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // CIE XYZ → camera (cam_xyz)
    double asShotMul[3] = {1, 1, 1};                  // as-shot multipliers (cam_mul)
};

// Lens/camera identity extracted from the RAW's EXIF, used to look up a Lensfun
// profile for automatic lens correction. Empty/zero fields mean "unknown".
struct LensMetadata {
    QString cameraMaker;     // e.g. "Canon"
    QString cameraModel;     // e.g. "EOS 5D Mark III"
    QString lensModel;       // e.g. "EF24-70mm f/2.8L II USM"
    float focalLength = 0;   // mm
    float aperture = 0;      // f-number at capture
    float focusDistance = 0; // metres (0 = unknown)
    ColorProfile color;      // camera colour matrices for white balance
};

// Known camera-RAW file extensions (lowercase, no dot). Single source of truth
// for both isRawPath() and the open-file dialog filter.
const QStringList &extensions();

// True if `path`'s extension is a known camera-RAW format (case-insensitive).
bool isRawPath(const QString &path);

// Decodes a RAW file to a 16-bit sRGB Image. Null Image + *error on failure. When
// `meta` is non-null it receives the camera/lens identity from EXIF. `opts`
// controls the automatic decode-time adjustments (default = historical behaviour).
Image decodeFile(const QString &path, QString *error = nullptr, LensMetadata *meta = nullptr,
                 const RawDecodeOptions &opts = {});

// Decodes a RAW image from an in-memory buffer (the source embedded in a .lumen).
Image decodeBytes(const void *data, qsizetype size, QString *error = nullptr,
                  LensMetadata *meta = nullptr, const RawDecodeOptions &opts = {});

} // namespace raw
