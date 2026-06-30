#pragma once

#include "core/Image.h"

#include <QJsonObject>

// A creative (post-crop) vignette: a radial gain that darkens (or lightens) the
// corners of the FRAMED image. It is applied as a finishing transform after the
// layer composite and after the crop (EditGraph::result → applyCrop →
// applyVignette), and mirrored in the GPU present pass (present.frag) so the
// preview matches the export. Because it runs after the crop it centres on the
// cropped frame and reaches its corners — the standard "post-crop" vignette.
//
// Distinct from LensCorrectionNode's vignetting *correction* (auto, Lensfun,
// pre-crop): this is the artistic effect.
struct VignetteParams {
    bool enabled = false;
    float amount = 0.0f;     // -100..+100: negative darkens corners (the common case)
    float midpoint = 50.0f;  // 0..100: how far out from centre the falloff begins
    float roundness = 0.0f;  // -100..+100: rectangular(-) ↔ circular(+)
    float feather = 50.0f;   // 0..100: softness of the falloff transition

    // True when this is a no-op (disabled or zero amount).
    bool isIdentity() const;

    // Clamps fields to their ranges.
    void sanitize();

    friend bool operator==(const VignetteParams &, const VignetteParams &) = default;

    QJsonObject toJson() const;
    static VignetteParams fromJson(const QJsonObject &);
};

// Multiplies the colour bands of `img` by the radial vignette gain (alpha
// untouched). Returns `img` unchanged when identity or null. The gain formula is
// mirrored verbatim in present.frag — see docs/VIGNETTE.md (the preview==export
// contract).
Image applyVignette(const Image &img, const VignetteParams &v);
