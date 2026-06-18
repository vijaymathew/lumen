#pragma once

#include "core/Image.h"

#include <QJsonObject>
#include <QRectF>

// A non-destructive crop + orientation, applied as the final geometric stage
// after the layer composite (EditGraph::result) and mirrored in the GPU present
// pass so preview == export. Axis-aligned only (v1): 90° rotation steps + flips
// + a crop rectangle; arbitrary straighten is future work.
struct CropState {
    int rotation = 0;        // clockwise quarter-turns: 0, 90, 180, 270
    bool flipH = false;      // mirror left/right
    bool flipV = false;      // mirror top/bottom
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

// Applies `crop` to `img`: flip → rotate (90° steps) → extract the rect. The
// rect is normalized in the oriented frame (after rotation 90/270 the frame is
// height×width). Returns `img` unchanged when the crop is identity or null.
Image applyCrop(const Image &img, const CropState &crop);
