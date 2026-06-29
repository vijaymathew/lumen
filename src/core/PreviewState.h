#pragma once

// PreviewState holds the GPU-preview parameters accumulated by walking the edit
// graph (EditGraph::previewState()). Each pointwise tone node contributes its
// effect here via EditNode::contributeToPreview(); the canvas applies the result
// in the fragment shader. This is how the edit graph drives the on-screen
// preview (DESIGN.md §5.1).
//
// Pointwise tone ops fold into this flat state. Spatial or strongly
// order-dependent nodes (blur, masks) will need a true multi-pass shader chain;
// that comes when such a node exists.
//
// Field order and packing must match the shader's std140 uniform block (see
// texture.frag) — CanvasWidget uploads these as contiguous floats.
struct PreviewState {
    // TuneNode — global tone.
    float exposure = 0.0f;     // EV stops, summed across nodes
    float contrast = 1.0f;     // factor, 1 = neutral, multiplied across nodes
    float saturation = 1.0f;   // factor, 1 = neutral, multiplied across nodes
    // LutNode.
    float lutIntensity = 1.0f; // look blend [0,1]; harmless when the LUT is identity
    // Mask-overlay fields. Selective adjustments are now masked layers (see
    // Layer/MaskSpec), so the in-shader sel* adjustment is vestigial (selEnabled
    // stays 0); selMaskView/selMaskMode/selInvert + the selMask texture drive the
    // preview-only "show mask" overlay of the active layer's mask.
    float selEnabled = 0.0f;     // 0/1 (no longer driven; kept for layout)
    float selLow = 0.0f;         // luminosity range [0,1] (overlay luminosity path)
    float selHigh = 1.0f;
    float selFeather = 0.1f;
    float selExposure = 0.0f;    // EV stops (vestigial)
    float selContrast = 1.0f;    // factor (vestigial)
    float selSaturation = 1.0f;  // factor (vestigial)
    float selMaskView = 0.0f;    // preview-only: 0 off, 1 red overlay, 2 grayscale
    float selMaskMode = 0.0f;    // 0 luminosity (parametric), 1 texture (uploaded mask)
    float selInvert = 0.0f;      // 1 = invert the mask
    float layerOpacity = 1.0f;   // this layer's blend onto the running result
    // MonoNode — monochrome conversion + toning. Weights are pre-normalised
    // (sum 1) and the tint is pre-normalised to luma 1 so the shader is a plain
    // dot + per-channel multiply (matches MonoNode::apply()).
    float monoEnabled = 0.0f;       // 0/1
    float monoR = 0.2126f;          // B&W mix weights (Rec.709 luma by default)
    float monoG = 0.7152f;
    float monoB = 0.0722f;
    float monoToneStrength = 0.0f;  // tint blend [0,1]
    float monoToneR = 1.0f;         // tint colour, normalised to luma 1
    float monoToneG = 1.0f;
    float monoToneB = 1.0f;
    // TuneNode — white balance as a linear-light 3x3 matrix (row-major), applied
    // before exposure (encoded → ^2.2 → W·rgb → ^(1/2.2)); identity = neutral.
    // Accumulated across nodes by matrix multiply. The nine scalars pack tightly
    // in std140 (matching the flat-float convention); the shader reassembles a
    // mat3. Matches TuneNode::whiteBalanceMatrix / apply().
    float wb00 = 1.0f, wb01 = 0.0f, wb02 = 0.0f;
    float wb10 = 0.0f, wb11 = 1.0f, wb12 = 0.0f;
    float wb20 = 0.0f, wb21 = 0.0f, wb22 = 1.0f;
    // ColorGradeNode — Lift/Gamma/Gain as per-channel Slope/Offset/Power, applied
    // in encoded space after the tone curves (`v·slope+offset)^power`). Identity =
    // slope 1, offset 0, power 1. Matches ColorGradeNode::resolveSOP / apply().
    float gradeEnabled = 0.0f;
    float gradeSlope0 = 1.0f, gradeSlope1 = 1.0f, gradeSlope2 = 1.0f;
    float gradeOffset0 = 0.0f, gradeOffset1 = 0.0f, gradeOffset2 = 0.0f;
    float gradePower0 = 1.0f, gradePower1 = 1.0f, gradePower2 = 1.0f;
    // Strength of the preview-only "show mask" overlay [0,1]. Set to the active
    // selective layer's opacity so the highlight visibly fades as opacity drops;
    // 1 while painting so strokes stay fully visible. Doesn't affect the export.
    float selMaskOpacity = 1.0f;

    friend bool operator==(const PreviewState &, const PreviewState &) = default;
};
