#pragma once

// PreviewState holds the GPU-preview parameters accumulated by walking the edit
// graph (EditGraph::previewState()). Each pointwise tone node contributes its
// effect here via EditNode::contributeToPreview(); the canvas applies the result
// in the fragment shader. This is how the edit graph drives the on-screen
// preview (DESIGN.md §5.1).
//
// Pointwise tone ops (exposure, and later contrast/saturation/etc.) fold into
// this flat state. Spatial or strongly order-dependent nodes (blur, masks) will
// need a true multi-pass shader chain; that comes when such a node exists.
struct PreviewState {
    float exposure = 0.0f; // EV stops, summed across enabled tune nodes
};
