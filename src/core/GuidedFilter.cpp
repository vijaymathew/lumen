#include "core/GuidedFilter.h"

#include <algorithm>
#include <vector>

namespace {

// Mean over a (2r+1)x(2r+1) window, via an integral image (O(1) per pixel).
std::vector<float> boxMean(const std::vector<float> &in, int w, int h, int r)
{
    const int sw = w + 1;
    std::vector<double> ii(static_cast<size_t>(sw) * (h + 1), 0.0);
    for (int y = 0; y < h; ++y) {
        double rowSum = 0.0;
        for (int x = 0; x < w; ++x) {
            rowSum += in[static_cast<size_t>(y) * w + x];
            ii[static_cast<size_t>(y + 1) * sw + (x + 1)] =
                ii[static_cast<size_t>(y) * sw + (x + 1)] + rowSum;
        }
    }

    std::vector<float> out(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - r);
        const int y1 = std::min(h - 1, y + r);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - r);
            const int x1 = std::min(w - 1, x + r);
            const double sum = ii[static_cast<size_t>(y1 + 1) * sw + (x1 + 1)]
                             - ii[static_cast<size_t>(y0) * sw + (x1 + 1)]
                             - ii[static_cast<size_t>(y1 + 1) * sw + x0]
                             + ii[static_cast<size_t>(y0) * sw + x0];
            const int count = (x1 - x0 + 1) * (y1 - y0 + 1);
            out[static_cast<size_t>(y) * w + x] = static_cast<float>(sum / count);
        }
    }
    return out;
}

} // namespace

std::vector<float> guidedFilter(const std::vector<float> &guide,
                                const std::vector<float> &src,
                                int w, int h, int radius, float eps)
{
    const size_t n = static_cast<size_t>(w) * h;
    if (guide.size() != n || src.size() != n || radius < 1)
        return src;

    std::vector<float> gg(n), gs(n);
    for (size_t i = 0; i < n; ++i) {
        gg[i] = guide[i] * guide[i];
        gs[i] = guide[i] * src[i];
    }

    const std::vector<float> mG = boxMean(guide, w, h, radius);
    const std::vector<float> mS = boxMean(src, w, h, radius);
    const std::vector<float> mGG = boxMean(gg, w, h, radius);
    const std::vector<float> mGS = boxMean(gs, w, h, radius);

    std::vector<float> a(n), b(n);
    for (size_t i = 0; i < n; ++i) {
        const float varG = mGG[i] - mG[i] * mG[i];
        const float covGS = mGS[i] - mG[i] * mS[i];
        a[i] = covGS / (varG + eps);
        b[i] = mS[i] - a[i] * mG[i];
    }

    const std::vector<float> mA = boxMean(a, w, h, radius);
    const std::vector<float> mB = boxMean(b, w, h, radius);

    std::vector<float> q(n);
    for (size_t i = 0; i < n; ++i)
        q[i] = std::clamp(mA[i] * guide[i] + mB[i], 0.0f, 1.0f);
    return q;
}
