#pragma once

#include <cstdint>
#include <vector>

// In-place content-aware fill of the masked region of an RGBA8 (or RGB) image,
// using Telea's Fast Marching Method (2004). Pixels with mask > 127 are filled
// from surrounding known pixels, propagating inward from the region boundary.
// `radius` is the neighbourhood radius in pixels (typical 5). Alpha is untouched.
void inpaintTelea(uint8_t *rgba, int width, int height, int bands,
                  const std::vector<uint8_t> &mask, int radius);
