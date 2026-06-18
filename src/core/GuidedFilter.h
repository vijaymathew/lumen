#pragma once

#include <vector>

// Single-channel guided filter (He, Sun, Tang 2010). Edge-preserving smoothing
// of `src` guided by `guide` — used to refine a colour-affinity mask so its
// edges follow image structure.
//
// All buffers are row-major floats of size w*h in [0,1]. `radius` is the box
// window radius in pixels; `eps` is the regularization (larger = smoother).
// Implemented with integral-image box means (no external dependency).
std::vector<float> guidedFilter(const std::vector<float> &guide,
                                const std::vector<float> &src,
                                int w, int h, int radius, float eps);
