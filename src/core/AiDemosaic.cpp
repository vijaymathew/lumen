#include "core/AiDemosaic.h"

#include "core/AiModelStore.h"

#include <QStringLiteral>

// ===========================================================================
// Build without LUMEN_AI_DEMOSAIC: a no-op stub. aiDemosaic{Supported,Available}
// are false, runAiDemosaic() returns empty, and the RAW loader falls back to AHD.
// ===========================================================================
#ifndef LUMEN_AI_DEMOSAIC

namespace raw {

bool aiDemosaicSupported()
{
    return false;
}

bool aiDemosaicAvailable()
{
    return false;
}

std::vector<float> runAiDemosaic(const MosaicInput &, const ColorProfile &, QString *error)
{
    if (error)
        *error = QStringLiteral("AI demosaic is not available in this build");
    return {};
}

} // namespace raw

#else // LUMEN_AI_DEMOSAIC
// ===========================================================================
// ONNX Runtime inference path.
//
// Model I/O contract (the .onnx must match):
//   input  "input"  : float NCHW [1, 4, H/2, W/2], the sensor mosaic packed to
//                     canonical RGGB planes in channel order [R, Gr, Gb, B],
//                     values 0..1, black-subtracted and white-balanced to grey.
//   output "output" : float NCHW [1, 3, H, W], linear camera-RGB (WB-neutral),
//                     nominally 0..1. This module applies camera→sRGB + the sRGB
//                     transfer function to reach Lumen's working scale.
// Input/output tensor names are read from the model, so only the shapes/order
// above are load-bearing.
//
// Model discovery: $LUMEN_DEMOSAIC_MODEL, else <AppDataLocation>/models/
// demosaic.onnx. Absent → aiDemosaicAvailable() is false and the option is
// hidden. Bayer only; X-Trans/Foveon never reach here (MosaicInput::bayer).
// ===========================================================================

#include <onnxruntime_cxx_api.h>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace {

// --- Small 3x3 colour-matrix helpers (row-major) -------------------------
using Mat3 = std::array<double, 9>;

Mat3 invert3(const Mat3 &m)
{
    const double a = m[0], b = m[1], c = m[2];
    const double d = m[3], e = m[4], f = m[5];
    const double g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    double det = a * A + b * B + c * C;
    if (std::abs(det) < 1e-12)
        return {1, 0, 0, 0, 1, 0, 0, 0, 1}; // singular → identity (safe fallback)
    const double inv = 1.0 / det;
    return {A * inv,             B * inv,             C * inv,
            (c * h - b * i) * inv, (a * i - c * g) * inv, (b * g - a * h) * inv,
            (b * f - c * e) * inv, (c * d - a * f) * inv, (a * e - b * d) * inv};
}

Mat3 mul3(const Mat3 &a, const Mat3 &b)
{
    Mat3 r{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r[row * 3 + col] = a[row * 3 + 0] * b[0 * 3 + col]
                             + a[row * 3 + 1] * b[1 * 3 + col]
                             + a[row * 3 + 2] * b[2 * 3 + col];
    return r;
}

// Camera(linear, WB-neutral) → linear sRGB. Derived from cam_xyz (XYZ→camera),
// which LibRaw fills at identify time, so this does NOT depend on dcraw_process
// having run — the AI path never calls it.
Mat3 cameraToSrgb(const raw::ColorProfile &c)
{
    const Mat3 xyzToCam = {c.xyzToCam[0], c.xyzToCam[1], c.xyzToCam[2],
                           c.xyzToCam[3], c.xyzToCam[4], c.xyzToCam[5],
                           c.xyzToCam[6], c.xyzToCam[7], c.xyzToCam[8]};
    // CIE XYZ (D65) → linear sRGB.
    static constexpr Mat3 kXyzToSrgb = {3.2404542, -1.5371385, -0.4985314,
                                        -0.9692660, 1.8760108, 0.0415560,
                                        0.0556434, -0.2040259, 1.0572252};
    return mul3(kXyzToSrgb, invert3(xyzToCam)); // XYZ→sRGB * camera→XYZ
}

inline float srgbOetf(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// --- Lazily-loaded ORT session (process-wide, thread-safe) ---------------
struct Model {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "lumen-demosaic"};
    Ort::Session session{nullptr};
    std::string inName, outName;
    QString loadedPath; // path currently loaded ("" = none)
    bool ok = false;
};

// Returns the session for the active model. Reloads if the user's model choice
// changed since last call (e.g. a just-downloaded or switched model), so no app
// restart is needed. Init is guarded by a mutex; the returned session's Run() is
// itself thread-safe, so callers use it without holding the lock.
Model &model()
{
    static Model m;
    static QMutex mtx;
    QMutexLocker lock(&mtx);
    const QString path = raw::aiActiveModelPath();
    if (path == m.loadedPath)
        return m; // already reflects the current choice (loaded or empty)

    m.loadedPath = path;
    m.session = Ort::Session(nullptr);
    m.ok = false;
    if (path.isEmpty())
        return m;
    try {
        Ort::SessionOptions so;
        so.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
#ifdef _WIN32
        m.session = Ort::Session(m.env, path.toStdWString().c_str(), so);
#else
        m.session = Ort::Session(m.env, path.toStdString().c_str(), so);
#endif
        Ort::AllocatorWithDefaultOptions alloc;
        m.inName = m.session.GetInputNameAllocated(0, alloc).get();
        m.outName = m.session.GetOutputNameAllocated(0, alloc).get();
        m.ok = true;
    } catch (const Ort::Exception &) {
        m.ok = false; // corrupt/incompatible model → caller falls back
    }
    return m;
}

// The four (dy,dx) sub-offsets within the 2x2 CFA cell for channels
// [R, Gr, Gb, B], derived from the per-pixel colour map. Green in R's row is Gr;
// green in B's row is Gb.
struct BayerPhase {
    int dy[4], dx[4];
};
BayerPhase phaseOf(const raw::MosaicInput &in)
{
    int rdy = 0, rdx = 0, bdy = 1, bdx = 1;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const uint8_t c = in.cfa[static_cast<size_t>(dy) * in.width + dx];
            if (c == 0) { rdy = dy; rdx = dx; }
            else if (c == 2) { bdy = dy; bdx = dx; }
        }
    }
    return {{rdy, rdy, bdy, bdy}, {rdx, 1 - rdx, 1 - bdx, bdx}};
}

// Run one packed tile [1,4,th,tw] through the model → [1,3,2*th,2*tw] linear
// camera RGB, written planar into `outTile` (size 3*2th*2tw). False on failure.
bool inferTile(Model &m, const std::vector<float> &packed, int tw, int th,
               std::vector<float> &outTile)
{
    const std::array<int64_t, 4> ishape{1, 4, th, tw};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(
        mem, const_cast<float *>(packed.data()), packed.size(), ishape.data(), ishape.size());
    const char *inNames[] = {m.inName.c_str()};
    const char *outNames[] = {m.outName.c_str()};
    try {
        auto outs = m.session.Run(Ort::RunOptions{nullptr}, inNames, &input, 1, outNames, 1);
        const auto shape = outs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[1] != 3
            || shape[2] != 2 * th || shape[3] != 2 * tw)
            return false;
        const float *od = outs[0].GetTensorData<float>();
        outTile.assign(od, od + static_cast<size_t>(3) * (2 * th) * (2 * tw));
        return true;
    } catch (const Ort::Exception &) {
        return false;
    }
}

} // namespace

namespace raw {

bool aiDemosaicSupported()
{
    return true;
}

bool aiDemosaicAvailable()
{
    return model().ok;
}

std::vector<float> runAiDemosaic(const MosaicInput &in, const ColorProfile &color, QString *error)
{
    if (!in.bayer || in.width < 4 || in.height < 4) {
        if (error)
            *error = QStringLiteral("AI demosaic requires a Bayer sensor");
        return {};
    }
    Model &m = model();
    if (!m.ok) {
        if (error)
            *error = QStringLiteral("AI demosaic model unavailable");
        return {};
    }

    const BayerPhase ph = phaseOf(in);
    const int W = in.width, H = in.height;
    const int pw = W / 2, phH = H / 2;       // packed (half-res) dimensions
    const int outW = pw * 2, outH = phH * 2; // output (may crop a stray odd edge)

    // Tile in the packed domain with a halo so tile seams don't show. Output
    // tiles are 2x the packed tile. Sized for a modest per-tile memory budget;
    // full-frame sensors would otherwise exhaust GPU/CPU memory in one pass.
    constexpr int kTile = 256; // packed px → 512 output px
    constexpr int kHalo = 8;   // packed px of overlap, discarded on stitch

    std::vector<float> lin(static_cast<size_t>(3) * outW * outH); // planar linear camera RGB
    std::vector<float> packed, outTile;

    for (int ty = 0; ty < phH; ty += kTile) {
        for (int tx = 0; tx < pw; tx += kTile) {
            // Core (kept) region and haloed (fed) region, clamped to the image.
            const int cx0 = tx, cy0 = ty;
            const int cx1 = std::min(tx + kTile, pw), cy1 = std::min(ty + kTile, phH);
            const int hx0 = std::max(cx0 - kHalo, 0), hy0 = std::max(cy0 - kHalo, 0);
            const int hx1 = std::min(cx1 + kHalo, pw), hy1 = std::min(cy1 + kHalo, phH);
            const int tw = hx1 - hx0, thh = hy1 - hy0;

            // Pack the haloed tile to RGGB planes (channel-major).
            packed.assign(static_cast<size_t>(4) * tw * thh, 0.0f);
            for (int ch = 0; ch < 4; ++ch) {
                float *plane = packed.data() + static_cast<size_t>(ch) * tw * thh;
                for (int y = 0; y < thh; ++y) {
                    const int sy = (hy0 + y) * 2 + ph.dy[ch];
                    for (int x = 0; x < tw; ++x) {
                        const int sx = (hx0 + x) * 2 + ph.dx[ch];
                        plane[static_cast<size_t>(y) * tw + x] =
                            in.mono[static_cast<size_t>(sy) * W + sx];
                    }
                }
            }

            if (!inferTile(m, packed, tw, thh, outTile)) {
                if (error)
                    *error = QStringLiteral("AI demosaic inference failed");
                return {};
            }

            // Copy the core region (drop the halo) into the full-res planar buffer.
            const int otw = tw * 2; // output tile width
            for (int ch = 0; ch < 3; ++ch) {
                const float *op = outTile.data() + static_cast<size_t>(ch) * (thh * 2) * otw;
                float *dst = lin.data() + static_cast<size_t>(ch) * outW * outH;
                for (int y = cy0 * 2; y < cy1 * 2; ++y) {
                    const int oy = y - hy0 * 2;
                    for (int x = cx0 * 2; x < cx1 * 2; ++x) {
                        const int ox = x - hx0 * 2;
                        dst[static_cast<size_t>(y) * outW + x] =
                            op[static_cast<size_t>(oy) * otw + ox];
                    }
                }
            }
        }
    }

    // Linear camera RGB → linear sRGB → sRGB transfer → interleaved RGBA 0..255.
    const Mat3 camToSrgb = cameraToSrgb(color);
    const float *r = lin.data();
    const float *g = lin.data() + static_cast<size_t>(outW) * outH;
    const float *b = lin.data() + static_cast<size_t>(2) * outW * outH;
    std::vector<float> rgba(static_cast<size_t>(outW) * outH * 4);
    for (size_t i = 0, n = static_cast<size_t>(outW) * outH; i < n; ++i) {
        const double cr = r[i], cg = g[i], cb = b[i];
        const float sr = static_cast<float>(camToSrgb[0] * cr + camToSrgb[1] * cg + camToSrgb[2] * cb);
        const float sg = static_cast<float>(camToSrgb[3] * cr + camToSrgb[4] * cg + camToSrgb[5] * cb);
        const float sb = static_cast<float>(camToSrgb[6] * cr + camToSrgb[7] * cg + camToSrgb[8] * cb);
        rgba[i * 4 + 0] = srgbOetf(sr) * 255.0f;
        rgba[i * 4 + 1] = srgbOetf(sg) * 255.0f;
        rgba[i * 4 + 2] = srgbOetf(sb) * 255.0f;
        rgba[i * 4 + 3] = 255.0f;
    }
    return rgba;
}

} // namespace raw

#endif // LUMEN_AI_DEMOSAIC
