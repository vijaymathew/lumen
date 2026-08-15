#pragma once

#include "core/CropState.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QSizeF>

#include <cmath>

// Pure crop/orientation coordinate math shared by CanvasWidget's present pass
// (texXform, effectiveImageSize) and its mask-edit coordinate conversion
// (sourceNormFromOriented/orientedNormFromSource), unit-tested in isolation.
// Mirrors CanvasWidget::CropViewMode one-for-one; kept as a separate enum here
// so this header has no dependency on CanvasWidget.
namespace cropview {

enum class ViewMode { None, Applied, Editing, MaskEdit };

// Effective displayed image size (device-independent units relative to the
// source texture), accounting for orientation + crop in the given mode.
inline QSizeF effectiveImageSize(QSize textureSize, const CropState &crop, ViewMode mode)
{
    const double W = textureSize.width(), H = textureSize.height();
    if (mode == ViewMode::None || textureSize.isEmpty())
        return {W, H};
    const bool swap = (crop.rotation == 90 || crop.rotation == 270);
    const double ow = swap ? H : W;
    const double oh = swap ? W : H;
    if (mode == ViewMode::Editing || mode == ViewMode::MaskEdit)
        return {ow, oh}; // oriented full frame (rect ignored while editing)
    return {ow * crop.rect.width(), oh * crop.rect.height()}; // oriented + crop
}

// Present-pass texcoord transform: output unit-quad -> source [0,1], encoding
// the current crop/orientation. Identity in ViewMode::None. Mirrors core
// applyCrop (flip -> rotate -> extract). Built as FlipUndo * RotInv *
// StraightenInv * Crop (Crop applied to the vector first).
inline QMatrix4x4 texXform(QSize textureSize, const CropState &crop, ViewMode mode)
{
    QMatrix4x4 m; // identity
    if (mode == ViewMode::None)
        return m;

    // Crop: output unit -> oriented frame. Editing/mask-edit show the full oriented
    // frame (crop rect ignored); Applied extracts the crop rect.
    QRectF rect = (mode == ViewMode::Editing || mode == ViewMode::MaskEdit)
                      ? QRectF(0, 0, 1, 1)
                      : crop.rect;
    QMatrix4x4 cropM;
    cropM.translate(rect.x(), rect.y());
    cropM.scale(rect.width(), rect.height());

    // RotInv: oriented -> pre-rotation (flipped-source) normalized coords.
    QMatrix4x4 rotInv;
    switch (crop.rotation) {
    case 90: // (u,v) -> (v, 1-u)
        rotInv = QMatrix4x4(0, 1, 0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1);
        break;
    case 180: // (u,v) -> (1-u, 1-v)
        rotInv = QMatrix4x4(-1, 0, 0, 1, 0, -1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1);
        break;
    case 270: // (u,v) -> (1-v, u)
        rotInv = QMatrix4x4(0, -1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
        break;
    default:
        break; // 0 -> identity
    }

    // StraightenInv: undo the fine tilt in the oriented frame. The rotation is
    // done in pixel space (about the frame centre) so the non-square normalized
    // frame isn't sheared. Mirrors applyCrop's vips_rotate (the undo angle is
    // -straighten). Applied for the browse and crop-edit views; the mask-edit
    // view stays upright, since masks live in the un-oriented source and are
    // addressed through this same matrix (see sourceNormFromOriented).
    QMatrix4x4 straightenInv;
    if (std::abs(crop.straighten) > 1e-6 && !textureSize.isEmpty()
        && (mode == ViewMode::Applied || mode == ViewMode::Editing)) {
        const bool swap = (crop.rotation == 90 || crop.rotation == 270);
        const float ow = swap ? textureSize.height() : textureSize.width();
        const float oh = swap ? textureSize.width() : textureSize.height();
        straightenInv.translate(0.5f, 0.5f);
        straightenInv.scale(1.0f / ow, 1.0f / oh);
        straightenInv.rotate(static_cast<float>(-crop.straighten), 0.0f, 0.0f, 1.0f);
        straightenInv.scale(ow, oh);
        straightenInv.translate(-0.5f, -0.5f);
    }

    // FlipUndo: flips were applied to the source before rotation; undo in source.
    QMatrix4x4 flipUndo;
    if (crop.flipH) {
        flipUndo.translate(1.0f, 0.0f);
        flipUndo.scale(-1.0f, 1.0f);
    }
    if (crop.flipV) {
        flipUndo.translate(0.0f, 1.0f);
        flipUndo.scale(1.0f, -1.0f);
    }

    return flipUndo * rotInv * straightenInv * cropM;
}

// In ViewMode::MaskEdit the display is oriented but masks live in the
// un-oriented source frame; these convert a normalised point between the two
// (no-op in every other mode). `xform` must be texXform(textureSize, crop,
// mode) for the same state. oriented->source uses xform directly; source->
// oriented its inverse. Both map the unit square onto itself, so results stay
// in [0,1].
inline QPointF sourceNormFromOriented(QPointF orientedNorm, const QMatrix4x4 &xform,
                                      ViewMode mode)
{
    if (mode != ViewMode::MaskEdit)
        return orientedNorm;
    return xform.map(orientedNorm);
}

inline QPointF orientedNormFromSource(QPointF sourceNorm, const QMatrix4x4 &xform,
                                      ViewMode mode)
{
    if (mode != ViewMode::MaskEdit)
        return sourceNorm;
    bool invertible = false;
    const QMatrix4x4 inv = xform.inverted(&invertible);
    return invertible ? inv.map(sourceNorm) : sourceNorm;
}

} // namespace cropview
