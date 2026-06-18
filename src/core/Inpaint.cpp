#include "core/Inpaint.h"

#include <algorithm>
#include <climits>
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

namespace {

inline float luma8(const uint8_t *p)
{
    return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
}

} // namespace

void inpaintCriminisi(uint8_t *img, int w, int h, int bands,
                      const std::vector<uint8_t> &mask, int patchRadius)
{
    if (!img || w <= 0 || h <= 0 || bands < 3 || patchRadius < 1)
        return;
    if (mask.size() != static_cast<size_t>(w) * h)
        return;

    const int hp = patchRadius;
    // Exemplars are searched in a local window around each target patch — the
    // surrounding texture is local, and this keeps the search tractable.
    const int searchR = std::max(48, patchRadius * 12);

    std::vector<uint8_t> src(static_cast<size_t>(w) * h);
    std::vector<float> conf(static_cast<size_t>(w) * h);
    std::vector<float> gray(static_cast<size_t>(w) * h);

    long long remaining = 0;
    int minx = w, miny = h, maxx = -1, maxy = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            const bool s = mask[i] <= 127;
            src[i] = s ? 1 : 0;
            conf[i] = s ? 1.0f : 0.0f;
            gray[i] = luma8(img + static_cast<size_t>(i) * bands);
            if (!s) {
                ++remaining;
                minx = std::min(minx, x);
                maxx = std::max(maxx, x);
                miny = std::min(miny, y);
                maxy = std::max(maxy, y);
            }
        }
    }
    if (remaining == 0)
        return;

    const auto idx = [w](int x, int y) { return y * w + x; };
    long long guard = static_cast<long long>(w) * h + 16;

    while (remaining > 0 && guard-- > 0) {
        const int bx0 = std::max(0, minx - 1), bx1 = std::min(w - 1, maxx + 1);
        const int by0 = std::max(0, miny - 1), by1 = std::min(h - 1, maxy + 1);

        // Highest-priority fill-front pixel: P = confidence * data (isophote).
        double bestP = -1.0;
        int bpx = -1, bpy = -1;
        float bestConf = 0.0f;
        for (int y = by0; y <= by1; ++y) {
            for (int x = bx0; x <= bx1; ++x) {
                const int i = idx(x, y);
                if (src[i])
                    continue;
                const bool front = (x > 0 && src[i - 1]) || (x < w - 1 && src[i + 1])
                                || (y > 0 && src[i - w]) || (y < h - 1 && src[i + w]);
                if (!front)
                    continue;

                double csum = 0.0;
                int cnt = 0;
                for (int dy = -hp; dy <= hp; ++dy) {
                    const int yy = y + dy;
                    if (yy < 0 || yy >= h)
                        continue;
                    for (int dx = -hp; dx <= hp; ++dx) {
                        const int xx = x + dx;
                        if (xx < 0 || xx >= w)
                            continue;
                        ++cnt;
                        if (src[idx(xx, yy)])
                            csum += conf[idx(xx, yy)];
                    }
                }
                const double C = cnt ? csum / cnt : 0.0;

                // Isophote (perpendicular to the grey gradient) and fill-front
                // normal (gradient of the source indicator).
                const int xl = x > 0 ? i - 1 : i, xr = x < w - 1 ? i + 1 : i;
                const int yu = y > 0 ? i - w : i, yd = y < h - 1 ? i + w : i;
                const float gx = 0.5f * (gray[xr] - gray[xl]);
                const float gy = 0.5f * (gray[yd] - gray[yu]);
                const float isoX = gy, isoY = -gx;
                float nx = (x < w - 1 ? (src[i + 1] ? 1.0f : 0.0f) : 0.0f)
                         - (x > 0 ? (src[i - 1] ? 1.0f : 0.0f) : 0.0f);
                float ny = (y < h - 1 ? (src[i + w] ? 1.0f : 0.0f) : 0.0f)
                         - (y > 0 ? (src[i - w] ? 1.0f : 0.0f) : 0.0f);
                const float nlen = std::sqrt(nx * nx + ny * ny);
                double D = 0.0;
                if (nlen > 1e-5f)
                    D = std::abs(isoX * (nx / nlen) + isoY * (ny / nlen)) / 255.0;

                const double P = C * (D + 0.001); // small term avoids stalls
                if (P > bestP) {
                    bestP = P;
                    bpx = x;
                    bpy = y;
                    bestConf = static_cast<float>(C);
                }
            }
        }
        if (bpx < 0)
            break;

        // Best-matching all-source exemplar patch (min SSD over known pixels).
        long long bestSSD = LLONG_MAX;
        int bqx = -1, bqy = -1;
        const int wx0 = std::max(hp, bpx - searchR), wx1 = std::min(w - 1 - hp, bpx + searchR);
        const int wy0 = std::max(hp, bpy - searchR), wy1 = std::min(h - 1 - hp, bpy + searchR);
        for (int qy = wy0; qy <= wy1 && bestSSD != 0; ++qy) {
            for (int qx = wx0; qx <= wx1; ++qx) {
                long long ssd = 0;
                bool ok = true;
                for (int dy = -hp; dy <= hp && ok; ++dy) {
                    const int ty = bpy + dy, sy = qy + dy;
                    if (ty < 0 || ty >= h)
                        continue;
                    for (int dx = -hp; dx <= hp; ++dx) {
                        const int tx = bpx + dx, sx = qx + dx;
                        if (tx < 0 || tx >= w)
                            continue;
                        const int si = idx(sx, sy);
                        if (!src[si]) { // candidate patch must be all-source
                            ok = false;
                            break;
                        }
                        const int ti = idx(tx, ty);
                        if (src[ti]) {
                            const uint8_t *a = img + static_cast<size_t>(ti) * bands;
                            const uint8_t *b = img + static_cast<size_t>(si) * bands;
                            for (int c = 0; c < 3; ++c) {
                                const int d = a[c] - b[c];
                                ssd += d * d;
                            }
                        }
                    }
                }
                if (ok && ssd < bestSSD) {
                    bestSSD = ssd;
                    bqx = qx;
                    bqy = qy;
                    if (bestSSD == 0) // exact match — can't do better
                        break;
                }
            }
        }

        if (bqx < 0) {
            // No valid exemplar (shouldn't happen): mark the pixel known so the
            // fill keeps progressing.
            const int i = idx(bpx, bpy);
            src[i] = 1;
            conf[i] = bestConf;
            --remaining;
            continue;
        }

        // Copy the unknown target pixels from the chosen exemplar.
        for (int dy = -hp; dy <= hp; ++dy) {
            const int ty = bpy + dy, sy = bqy + dy;
            if (ty < 0 || ty >= h)
                continue;
            for (int dx = -hp; dx <= hp; ++dx) {
                const int tx = bpx + dx, sx = bqx + dx;
                if (tx < 0 || tx >= w)
                    continue;
                const int ti = idx(tx, ty);
                if (src[ti])
                    continue;
                const int si = idx(sx, sy);
                uint8_t *a = img + static_cast<size_t>(ti) * bands;
                const uint8_t *b = img + static_cast<size_t>(si) * bands;
                for (int c = 0; c < 3; ++c)
                    a[c] = b[c];
                src[ti] = 1;
                conf[ti] = bestConf;
                gray[ti] = luma8(a);
                --remaining;
            }
        }
    }
}
