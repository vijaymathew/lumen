#pragma once

#include "core/Image.h"

#include <QJsonObject>
#include <QRectF>

// A non-destructive crop + orientation, applied as the final geometric stage
// after the layer composite (EditGraph::result) and mirrored in the GPU present
// pass so preview == export. Supports 90° rotation steps + flips + a fine
// straighten angle (level-the-horizon) + a crop rectangle.
struct CropState {
    int rotation = 0;        // clockwise quarter-turns: 0, 90, 180, 270
    bool flipH = false;      // mirror left/right
    bool flipV = false;      // mirror top/bottom
    double straighten = 0.0; // fine clockwise rotation in degrees, [-45, 45],
                             // about the oriented frame's centre. The crop rect
                             // stays axis-aligned; content tilts beneath it.
    QRectF rect{0.0, 0.0, 1.0, 1.0}; // crop, normalized within the ORIENTED frame
    bool enabled = true;     // toggled off → applyCrop is a no-op (geometry kept)

    // True when the geometry is a no-op (full frame, no rotation/flip). Independent
    // of `enabled`, so a disabled-but-cropped state still reports as non-identity
    // (it remains an "adjustment" you can re-enable).
    bool isIdentity() const;

    // Normalises rotation to {0,90,180,270} and clamps rect into [0,1] with a
    // minimum size; call after mutating fields.
    void sanitize();

    friend bool operator==(const CropState &, const CropState &) = default;

    QJsonObject toJson() const;
    static CropState fromJson(const QJsonObject &);
};

// Largest axis-aligned crop rectangle, centred and normalized within an oriented
// frame of size `frameW`×`frameH`, that stays inside that frame after it is
// rotated by `angleDeg` about its centre — i.e. clear of the transparent corners
// a straighten introduces. `desiredAspect` is the crop's width/height in oriented
// pixels; pass <= 0 for a free aspect (maximises area). At angle 0 this returns
// the full frame {0,0,1,1}. Used to auto-inset the crop rect when straightening.
QRectF straightenSafeRect(double angleDeg, double frameW, double frameH,
                          double desiredAspect);

// Applies `crop` to `img`: flip → rotate (90° steps) → straighten → extract the
// rect. The rect is normalized in the oriented frame (after rotation 90/270 the
// frame is height×width); the straighten angle rotates content about the frame
// centre before the axis-aligned rect is extracted. Returns `img` unchanged when
// the crop is identity or null.
Image applyCrop(const Image &img, const CropState &crop);
