#pragma once

#include "core/Lut.h"
#include "core/Lut3D.h"
#include "core/PreviewState.h"
#include "core/SelectiveMask.h"

// The GPU-preview data for one layer (LAYERS.md §4.2): its accumulated tone
// state, curves, look, the selective node's mask, the layer's own mask coverage,
// and opacity. The canvas renders one pass per layer from this.
struct LayerPreview {
    PreviewState state;
    ChannelLuts curves = identityChannelLuts();
    Lut3D look;
    MaskBuffer selMask;   // selective node mask (legacy / Base layer)
    MaskBuffer layerMask; // this layer's MaskSpec coverage; empty = full
    float opacity = 1.0f;
};
