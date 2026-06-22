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
    float exposure = 0.0f;     // EV stops, summed across nodes
    float contrast = 1.0f;     // factor, 1 = neutral, multiplied across nodes
    float saturation = 1.0f;   // factor, 1 = neutral, multiplied across nodes
    float lutIntensity = 1.0f; // look blend [0,1]; harmless when the LUT is identity
};
