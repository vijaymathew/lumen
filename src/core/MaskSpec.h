#pragma once

#include "core/SelectiveMask.h"

#include <QJsonObject>
#include <QPointF>
#include <QVector>

#include <vector>

// One shape of a mask's "exclusive zone": an area where masking is allowed to
// act. Shapes union together to define the included region (additive) and can
// cut holes out of it (subtract). Geometry is image-normalised, matching the
// evaluateMask coordinate convention (px = (x+0.5)/w), so — like the Radial
// mask — a "circle" with equal normalised radii renders as an ellipse on a
// non-square image, keeping preview==export.
struct MaskZoneShape {
    enum Kind { Rect, Ellipse, Polygon }; // circle = Ellipse with equal radii

    Kind kind = Rect;
    bool subtract = false; // true: cut this shape out of the included region

    // Rect / Ellipse: centre + half-extents (radii) + rotation.
    QPointF center{0.5, 0.5};
    QPointF half{0.2, 0.2};
    float angle = 0.0f; // radians

    // Polygon (freehand): boundary vertices, image-normalised.
    QVector<QPointF> points;

    friend bool operator==(const MaskZoneShape &, const MaskZoneShape &) = default;

    QJsonObject toJson() const;
    static MaskZoneShape fromJson(const QJsonObject &);
};

// A serializable description of a layer's mask (LAYERS.md §3). Most types are
// parametric — evaluated to per-pixel coverage [0,1] by identical math in
// libvips (evaluateMask, here) and, later, the preview shader. This keeps masks
// resolution-independent and preview==export.
struct MaskSpec {
    enum Type { None, Brush, LinearGradient, Radial, Luminosity, Colour };

    Type type = None;
    bool invert = false;
    float feather = 0.1f; // edge softness [0,1]; meaning depends on type

    // LinearGradient: coverage ramps 0→1 along from→to (image-normalised).
    QPointF gradFrom{0.0, 0.5};
    QPointF gradTo{1.0, 0.5};

    // Radial / elliptical: centre + radii (image-normalised) + rotation.
    QPointF center{0.5, 0.5};
    float radiusX = 0.3f;
    float radiusY = 0.3f;
    float angle = 0.0f;        // radians
    bool radialInside = true;  // true: select inside the ellipse

    // Luminosity range.
    float low = 0.0f;
    float high = 1.0f;

    // Colour affinity.
    float targetR = 0.0f;
    float targetG = 0.0f;
    float targetB = 0.0f;
    float colorRange = 0.3f;

    // Brush mask (type == Brush): a painted bitmap at working resolution,
    // upscaled when evaluated. (Brush-refinement of parametric masks is a later
    // step; for now this is only used when type == Brush.)
    MaskBuffer brush;

    // Exclusive zone: shapes that gate where the mask (of any type) may act.
    // Empty = no restriction (the whole image). Applied as a coverage multiplier
    // in evaluateMask, so it affects both the GPU preview and the export.
    std::vector<MaskZoneShape> zones;
    float zoneFeather = 0.05f; // soft border for the zone edges [0,1]

    friend bool operator==(const MaskSpec &, const MaskSpec &) = default;

    QJsonObject toJson() const;
    static MaskSpec fromJson(const QJsonObject &);
};

// Evaluates `spec` to per-pixel coverage [0,1] at width x height. `rgba`
// (row-major, `bands` per pixel, >=3) is required for Luminosity/Colour masks
// and ignored otherwise (pass nullptr for purely geometric masks).
MaskBuffer evaluateMask(const MaskSpec &spec, int width, int height,
                        const uint8_t *rgba = nullptr, int bands = 4);
