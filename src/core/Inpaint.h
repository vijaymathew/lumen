#pragma once

#include <cstdint>
#include <vector>

// In-place content-aware fill of the masked region of an RGBA8 (or RGB) image,
// using Telea's Fast Marching Method (2004). Pixels with mask > 127 are filled
// from surrounding known pixels, propagating inward from the region boundary.
// `radius` is the neighbourhood radius in pixels (typical 5). Alpha is untouched.
// Fast, but diffuses (smears) over textured regions.
void inpaintTelea(uint8_t *rgba, int width, int height, int bands,
                  const std::vector<uint8_t> &mask, int radius);

// In-place exemplar-based inpainting (Criminisi, Pérez, Toyama 2004). Fills the
// masked region (mask > 127) patch-by-patch along isophotes, copying the best-
// matching real patch from the surrounding source region — so it reproduces
// texture rather than smearing. `patchRadius` is the half patch size (4 → 9x9).
// Slower than Telea; best for blemish/object removal. Alpha is untouched.
void inpaintCriminisi(uint8_t *rgba, int width, int height, int bands,
                      const std::vector<uint8_t> &mask, int patchRadius);
