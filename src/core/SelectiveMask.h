#pragma once

#include <QString>

#include <cstdint>
#include <vector>

// A single-channel float mask, row-major in [0,1].
struct MaskBuffer {
    int width = 0;
    int height = 0;
    std::vector<float> data;

    bool isEmpty() const { return data.empty(); }

    friend bool operator==(const MaskBuffer &, const MaskBuffer &) = default;
};

// Mask <-> base64 PNG, for persisting a bulky painted mask in node state.
// Empty mask <-> empty string.
QString encodeMaskPng(const MaskBuffer &mask);
MaskBuffer decodeMaskPng(const QString &base64Png);

// Colour-affinity mask: per-pixel falloff by colour distance to `target`
// ([0,1] RGB), refined with a guided filter (guide = luminance) so the mask
// edges follow image structure. `range` is the colour-distance tolerance [0,1].
// `rgba` is row-major 8-bit with `bands` (>=3) per pixel.
MaskBuffer colorAffinityMask(const uint8_t *rgba, int width, int height, int bands,
                             float tr, float tg, float tb, float range);

// Paints a soft circular brush stamp into `mask` at (cx, cy) in mask-pixel
// coords, `radius` in pixels with `hardness` [0,1] edge softness. `add` paints
// toward 1 (selected); otherwise toward 0 (deselected).
void stampBrush(MaskBuffer &mask, float cx, float cy, float radius, float hardness,
                bool add);

// Bilinearly resamples `src` to width x height.
MaskBuffer upscaleMask(const MaskBuffer &src, int width, int height);
