#pragma once

#include <array>
#include <cstdint>

class Image;

// Per-channel 256-bin RGB histogram of an image's display-referred values
// (sRGB, clamped 0..255). Computed from a downsampled copy — enough for a UI
// histogram, cheap enough to refresh as edits settle.
struct HistogramData {
    std::array<std::array<uint32_t, 256>, 3> bins{}; // [channel R,G,B][level]
    uint32_t peak = 0;                               // max bin count (for scaling)
    bool valid = false;
};

// Computes the histogram of `image`. Returns an invalid (all-zero) result for a
// null image. `maxDim` caps the longest side of the analysed copy (speed).
HistogramData computeHistogram(const Image &image, int maxDim = 512);
