#pragma once

#include <cstdint>
#include <vector>

// A single-channel float mask, row-major in [0,1].
struct MaskBuffer {
    int width = 0;
    int height = 0;
    std::vector<float> data;

    bool isEmpty() const { return data.empty(); }
};

// Colour-affinity mask: per-pixel falloff by colour distance to `target`
// ([0,1] RGB), refined with a guided filter (guide = luminance) so the mask
// edges follow image structure. `range` is the colour-distance tolerance [0,1].
// `rgba` is row-major 8-bit with `bands` (>=3) per pixel.
MaskBuffer colorAffinityMask(const uint8_t *rgba, int width, int height, int bands,
                             float tr, float tg, float tb, float range);
