#include "core/Inpaint.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

// Telea's Fast Marching Method inpainting (A. Telea, 2004, "An Image Inpainting
// Technique Based on the Fast Marching Method"). The unknown region is filled
// boundary-inward: each pixel's colour is a weighted average of nearby known
// pixels, the weights favouring pixels close to it, near the propagation
// normal, and on the same level set (isophote).

namespace {

// Per-pixel flags.
enum Flag : uint8_t { KNOWN = 0, BAND = 1, INSIDE = 2 };

struct HeapItem {
    float t;
    int idx;
    bool operator>(const HeapItem &o) const { return t > o.t; }
};

constexpr float kInf = 1.0e6f;

// Solve the Eikonal equation |grad T| = 1 at (i,j) from up to two known
// neighbours (Telea eq. via the quadratic), used to assign arrival times T.
float solveEikonal(int i1, int j1, int i2, int j2, int w, int h,
                   const std::vector<float> &T, const std::vector<uint8_t> &flag)
{
    const auto valid = [&](int i, int j) {
        return i >= 0 && i < h && j >= 0 && j < w && flag[i * w + j] != INSIDE;
    };
    float sol = kInf;
    const float t1 = valid(i1, j1) ? T[i1 * w + j1] : kInf;
    const float t2 = valid(i2, j2) ? T[i2 * w + j2] : kInf;
    if (t1 < kInf && t2 < kInf) {
        const float d = 2.0f - (t1 - t2) * (t1 - t2);
        if (d > 0.0f) {
            const float r = std::sqrt(d);
            float s = (t1 + t2 - r) * 0.5f;
            if (s >= t1 && s >= t2)
                sol = s;
            else {
                s = (t1 + t2 + r) * 0.5f;
                if (s >= t1 && s >= t2)
                    sol = s;
            }
        }
    } else if (t1 < kInf) {
        sol = 1.0f + t1;
    } else if (t2 < kInf) {
        sol = 1.0f + t2;
    }
    return sol;
}

// Fill pixel (i,j) by Telea's weighted average over the known neighbourhood.
void inpaintPixel(int i, int j, int radius, int w, int h, int bands,
                  uint8_t *img, const std::vector<float> &T,
                  const std::vector<uint8_t> &flag)
{
    // Gradient of T at (i,j) — the propagation direction.
    float gradTx = 0.0f, gradTy = 0.0f;
    {
        const int n = (i + 1 < h && flag[(i + 1) * w + j] != INSIDE) ? (i + 1) * w + j : -1;
        const int s = (i - 1 >= 0 && flag[(i - 1) * w + j] != INSIDE) ? (i - 1) * w + j : -1;
        if (n >= 0 && s >= 0)
            gradTy = (T[n] - T[s]) * 0.5f;
        else if (n >= 0)
            gradTy = T[n] - T[i * w + j];
        else if (s >= 0)
            gradTy = T[i * w + j] - T[s];

        const int e = (j + 1 < w && flag[i * w + (j + 1)] != INSIDE) ? i * w + (j + 1) : -1;
        const int west = (j - 1 >= 0 && flag[i * w + (j - 1)] != INSIDE) ? i * w + (j - 1) : -1;
        if (e >= 0 && west >= 0)
            gradTx = (T[e] - T[west]) * 0.5f;
        else if (e >= 0)
            gradTx = T[e] - T[i * w + j];
        else if (west >= 0)
            gradTx = T[i * w + j] - T[west];
    }

    double accum[3] = {0.0, 0.0, 0.0};
    double wsum = 0.0;
    const int r2 = radius * radius;

    for (int di = -radius; di <= radius; ++di) {
        const int ni = i + di;
        if (ni < 0 || ni >= h)
            continue;
        for (int dj = -radius; dj <= radius; ++dj) {
            const int nj = j + dj;
            if (nj < 0 || nj >= w)
                continue;
            if (di * di + dj * dj > r2)
                continue;
            const int nidx = ni * w + nj;
            if (flag[nidx] != KNOWN)
                continue;

            // Telea weight = direction * distance * level-set terms.
            const float dst = 1.0f / (float(di * di + dj * dj));
            const float lev = 1.0f / (1.0f + std::abs(T[nidx] - T[i * w + j]));
            float dir = std::abs(-dj * gradTx + -di * gradTy);
            if (dir < 1.0e-6f)
                dir = 1.0e-6f;
            const float wgt = dst * lev * dir;

            for (int c = 0; c < 3; ++c)
                accum[c] += wgt * img[nidx * bands + c];
            wsum += wgt;
        }
    }

    if (wsum > 0.0) {
        for (int c = 0; c < 3; ++c)
            img[(i * w + j) * bands + c] =
                static_cast<uint8_t>(std::clamp(std::lround(accum[c] / wsum), 0L, 255L));
    }
}

} // namespace

void inpaintTelea(uint8_t *rgba, int width, int height, int bands,
                  const std::vector<uint8_t> &mask, int radius)
{
    if (!rgba || width <= 0 || height <= 0 || bands < 3 || radius < 1)
        return;
    if (mask.size() != static_cast<size_t>(width) * height)
        return;

    const int w = width, h = height;
    const size_t n = static_cast<size_t>(w) * h;

    std::vector<uint8_t> flag(n, KNOWN);
    std::vector<float> T(n, 0.0f);
    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> heap;

    for (size_t i = 0; i < n; ++i) {
        if (mask[i] > 127) {
            flag[i] = INSIDE;
            T[i] = kInf;
        }
    }

    // Seed the narrow band: known pixels 4-adjacent to the inside region.
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            if (flag[i * w + j] != INSIDE)
                continue;
            for (int k = 0; k < 4; ++k) {
                const int ni = i + dy[k], nj = j + dx[k];
                if (ni < 0 || ni >= h || nj < 0 || nj >= w)
                    continue;
                const int nidx = ni * w + nj;
                if (flag[nidx] == KNOWN) {
                    flag[nidx] = BAND;
                    T[nidx] = 0.0f;
                    heap.push({0.0f, nidx});
                }
            }
        }
    }

    // March inward.
    while (!heap.empty()) {
        const int idx = heap.top().idx;
        heap.pop();
        const int i = idx / w, j = idx % w;
        if (flag[idx] != BAND)
            continue;
        flag[idx] = KNOWN;

        for (int k = 0; k < 4; ++k) {
            const int ni = i + dy[k], nj = j + dx[k];
            if (ni < 0 || ni >= h || nj < 0 || nj >= w)
                continue;
            const int nidx = ni * w + nj;
            if (flag[nidx] == KNOWN)
                continue;

            const float t = std::min({
                solveEikonal(ni - 1, nj, ni, nj - 1, w, h, T, flag),
                solveEikonal(ni + 1, nj, ni, nj - 1, w, h, T, flag),
                solveEikonal(ni - 1, nj, ni, nj + 1, w, h, T, flag),
                solveEikonal(ni + 1, nj, ni, nj + 1, w, h, T, flag),
            });
            T[nidx] = t;

            if (flag[nidx] == INSIDE) {
                flag[nidx] = BAND;
                inpaintPixel(ni, nj, radius, w, h, bands, rgba, T, flag);
            }
            heap.push({t, nidx});
        }
    }
}
