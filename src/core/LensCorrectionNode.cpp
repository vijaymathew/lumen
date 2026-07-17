// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/LensCorrectionNode.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifdef LUMEN_HAVE_LENSFUN
#include <lensfun/lensfun.h>
#endif

namespace {

constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

using Mat3 = std::array<double, 9>; // row-major

Mat3 matMul(const Mat3 &a, const Mat3 &b)
{
    Mat3 c{};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col) {
            double s = 0;
            for (int k = 0; k < 3; ++k)
                s += a[r * 3 + k] * b[k * 3 + col];
            c[r * 3 + col] = s;
        }
    return c;
}

// vips_mapim only grew its "background" property in libvips 8.13. An older
// runtime fails the whole call on the unknown property, which would turn every
// resample below into a silent no-op. That is a live case, not a hypothetical:
// Ubuntu 22.04 (still in support, and what install.sh builds against there)
// ships 8.12. On those we accept the default (transparent) background — it only
// shows in the outermost corrected pixels.
bool mapimTakesBackground()
{
    static const bool ok =
        vips_version(0) > 8 || (vips_version(0) == 8 && vips_version(1) >= 13);
    return ok;
}

// Resample `in` through a per-pixel backward map (`map` is w*h*2 floats: source
// x then y for each destination pixel) using vips_mapim with bicubic
// interpolation. Out-of-range samples are filled with the background. Returns an
// owned image, or nullptr on failure.
VipsImage *resampleThroughMap(VipsImage *in, const std::vector<float> &map, int w, int h)
{
    VipsImage *index = vips_image_new_from_memory_copy(
        map.data(), map.size() * sizeof(float), w, h, 2, VIPS_FORMAT_FLOAT);
    if (!index)
        return nullptr;
    VipsInterpolate *interp = vips_interpolate_new("bicubic");
    VipsImage *out = nullptr;
    int rc = 0;
    if (mapimTakesBackground()) {
        // vips rejects a background whose arity is neither 1 nor the band count,
        // so size it to `in` (which is a single band on the per-channel TCA path).
        std::vector<double> bg(in->Bands, 0.0); // black, where defined
        if (in->Bands == 4)
            bg[3] = 255.0; // ... and opaque
        VipsArrayDouble *bgArr =
            vips_array_double_new(bg.data(), static_cast<int>(bg.size()));
        rc = vips_mapim(in, &out, index, "interpolate", interp, "background", bgArr,
                        nullptr);
        vips_area_unref(reinterpret_cast<VipsArea *>(bgArr));
    } else {
        rc = vips_mapim(in, &out, index, "interpolate", interp, nullptr);
    }
    if (rc) {
        // Every caller treats nullptr as "leave the image alone", so without this
        // a failure here is indistinguishable from a correction that did nothing.
        qWarning("lens: vips_mapim failed, correction skipped: %s", vips_error_buffer());
        vips_error_clear();
    }
    if (interp)
        g_object_unref(interp);
    g_object_unref(index);
    return rc ? nullptr : out;
}

} // namespace

std::array<double, 9> LensCorrectionNode::perspectiveBackMap(int width, int height,
                                                             float keystoneV,
                                                             float keystoneH,
                                                             float rotate, float scale)
{
    // Virtual-camera homography: tilt the image plane in 3D (pitch=vertical
    // keystone, yaw=horizontal keystone, roll=rotation) about the camera centre,
    // then zoom. Hfwd(source→dest) = S·K·R·K⁻¹ with R = Rz·Ry·Rx; the backward
    // map we need is Hfwd⁻¹ = K·Rᵀ·K⁻¹·S⁻¹ (R orthogonal ⇒ R⁻¹ = Rᵀ).
    const double cx = width * 0.5, cy = height * 0.5;
    const double f = std::max(width, height); // ~53° FOV virtual camera
    const double s = (scale > 1e-4f) ? scale : 1.0;

    const double a = keystoneV * kDeg2Rad; // pitch (about X)
    const double b = keystoneH * kDeg2Rad; // yaw   (about Y)
    const double g = rotate * kDeg2Rad;    // roll  (about Z)

    const Mat3 Rx = {1, 0, 0, 0, std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a)};
    const Mat3 Ry = {std::cos(b), 0, std::sin(b), 0, 1, 0, -std::sin(b), 0, std::cos(b)};
    const Mat3 Rz = {std::cos(g), -std::sin(g), 0, std::sin(g), std::cos(g), 0, 0, 0, 1};
    const Mat3 R = matMul(matMul(Rz, Ry), Rx);
    const Mat3 Rt = {R[0], R[3], R[6], R[1], R[4], R[7], R[2], R[5], R[8]}; // transpose

    const Mat3 K = {f, 0, cx, 0, f, cy, 0, 0, 1};
    const Mat3 Kinv = {1.0 / f, 0, -cx / f, 0, 1.0 / f, -cy / f, 0, 0, 1};
    // S⁻¹: zoom about centre by 1/s.
    const Mat3 Sinv = {1.0 / s, 0, cx * (1.0 - 1.0 / s),
                       0, 1.0 / s, cy * (1.0 - 1.0 / s),
                       0, 0, 1};

    const Mat3 H = matMul(matMul(matMul(K, Rt), Kinv), Sinv);

    // A camera tilt also shifts the framing; re-centre so the image centre is a
    // fixed point (keystone "keeps the subject put" — edges fan in/out around
    // it). Post-translate by whatever offset the centre picked up.
    const double w0 = H[6] * cx + H[7] * cy + H[8];
    const double sx0 = (H[0] * cx + H[1] * cy + H[2]) / w0;
    const double sy0 = (H[3] * cx + H[4] * cy + H[5]) / w0;
    const Mat3 T = {1, 0, cx - sx0, 0, 1, cy - sy0, 0, 0, 1};
    return matMul(T, H);
}

bool LensCorrectionNode::Params::operator==(const Params &o) const
{
    return cameraMaker == o.cameraMaker && cameraModel == o.cameraModel
        && lensModel == o.lensModel && focalLength == o.focalLength
        && aperture == o.aperture && focusDistance == o.focusDistance
        && cropFactor == o.cropFactor && distortion == o.distortion
        && distortionAmount == o.distortionAmount && tca == o.tca
        && vignetting == o.vignetting && vignettingAmount == o.vignettingAmount
        && keystoneV == o.keystoneV && keystoneH == o.keystoneH
        && rotate == o.rotate && scale == o.scale;
}

LensCorrectionNode::LensCorrectionNode()
    : EditNode(QStringLiteral("lens"))
{
}

void LensCorrectionNode::setParams(const Params &params)
{
    Params p = params;
    p.distortionAmount = std::clamp(p.distortionAmount, 0.0f, 1.0f);
    p.vignettingAmount = std::clamp(p.vignettingAmount, 0.0f, 1.0f);
    p.keystoneV = std::clamp(p.keystoneV, -kMaxKeystone, kMaxKeystone);
    p.keystoneH = std::clamp(p.keystoneH, -kMaxKeystone, kMaxKeystone);
    p.rotate = std::clamp(p.rotate, -kMaxRotate, kMaxRotate);
    p.scale = std::clamp(p.scale, kMinScale, kMaxScale);
    if (p != m_params) {
        m_params = p;
        m_matchCache = -1; // identity may have changed → re-query the DB
        invalidate();
    }
}

bool LensCorrectionNode::perspectiveActive() const
{
    const Params &p = m_params;
    return std::abs(p.keystoneV) > 1e-3f || std::abs(p.keystoneH) > 1e-3f
        || std::abs(p.rotate) > 1e-3f || std::abs(p.scale - 1.0f) > 1e-3f;
}

bool LensCorrectionNode::autoActive() const
{
    return (m_params.distortion || m_params.tca || m_params.vignetting)
        && lensMatched();
}

// ===================== Lensfun-backed automatic correction ==================
#ifdef LUMEN_HAVE_LENSFUN
namespace {

// The Lensfun database is loaded once and shared. Load() reads the profiles the
// distribution installed (liblensfun-data, which install.sh pulls in), which is
// what keeps them current: they track the host's lensfun, so a camera the system
// knows is a camera we match. Nothing is shipped alongside the binary — a pinned
// copy of the profiles goes stale against the cameras people actually own.
lfDatabase *lensDatabase()
{
    static lfDatabase *db = [] {
        auto *d = new lfDatabase();
        d->Load(); // best-effort: an empty DB simply matches nothing
        return d;
    }();
    return db;
}

// Resolves the lens profile for `p`. Returns the matched lens (owned by the DB,
// do not free) and fills `crop`, or nullptr when nothing matches. Needs at least
// a camera or an explicit lens string to have anything to look up.
const lfLens *findLens(const LensCorrectionNode::Params &p, float &crop)
{
    if (p.cameraModel.isEmpty() && p.lensModel.isEmpty())
        return nullptr;
    lfDatabase *db = lensDatabase();

    const lfCamera *cam = nullptr;
    if (!p.cameraModel.isEmpty()) {
        const lfCamera **cams = db->FindCamerasExt(
            p.cameraMaker.isEmpty() ? nullptr : p.cameraMaker.toUtf8().constData(),
            p.cameraModel.toUtf8().constData());
        if (cams && cams[0])
            cam = cams[0];
        if (cams)
            lf_free(cams);
    }

    const lfLens *lens = nullptr;
    if (!p.lensModel.isEmpty()) {
        const lfLens **lenses =
            db->FindLenses(cam, nullptr, p.lensModel.toUtf8().constData());
        if (lenses && lenses[0])
            lens = lenses[0];
        if (lenses)
            lf_free(lenses);
    }
    // Fixed-lens compacts carry no EXIF lens string — the profile is keyed off
    // the camera (its mount). Fall back to the camera's built-in lens.
    if (!lens && cam) {
        const lfLens **lenses = db->FindLenses(cam, nullptr, nullptr);
        if (lenses && lenses[0])
            lens = lenses[0];
        if (lenses)
            lf_free(lenses);
    }
    if (!lens)
        return nullptr;

    crop = p.cropFactor > 0 ? p.cropFactor : (cam ? cam->CropFactor : lens->CropFactor);
    return lens;
}

} // namespace

bool LensCorrectionNode::lensMatched() const
{
    if (m_matchCache < 0) {
        float crop = 0;
        const lfLens *lens = findLens(m_params, crop);
        m_matchCache = lens ? 1 : 0;
        m_matchedName = lens ? QString::fromUtf8(lens->Model) : QString();
    }
    return m_matchCache == 1;
}

QString LensCorrectionNode::matchedLensName() const
{
    lensMatched(); // populates the cache
    return m_matchedName;
}
#else  // !LUMEN_HAVE_LENSFUN
bool LensCorrectionNode::lensMatched() const
{
    return false;
}

QString LensCorrectionNode::matchedLensName() const
{
    return QString();
}
#endif // LUMEN_HAVE_LENSFUN

Image LensCorrectionNode::apply(const Image &input) const
{
    if (input.isNull())
        return input;
    const bool doAuto = autoActive();
    const bool doPersp = perspectiveActive();
    if (!doAuto && !doPersp)
        return input;

    // Materialise the float RGBA working buffer once.
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    const int w = f->Xsize, h = f->Ysize, bands = f->Bands;
    void *raw = vips_image_write_to_memory(f, nullptr);
    g_object_unref(f);
    if (!raw)
        return input;
    auto *px = static_cast<float *>(raw);
    const long long n = static_cast<long long>(w) * h;

#ifdef LUMEN_HAVE_LENSFUN
    // --- 1. Vignetting: a per-pixel gain on the source buffer (in place) -----
    if (doAuto && m_params.vignetting && bands >= 3) {
        float crop = 0;
        if (const lfLens *lens = findLens(m_params, crop)) {
            auto *mod = new lfModifier(lens, crop, w, h);
            mod->Initialize(lens, LF_PF_F32, m_params.focalLength, m_params.aperture,
                            m_params.focusDistance > 0 ? m_params.focusDistance : 1000.0f,
                            1.0f, lens->Type, LF_MODIFY_VIGNETTING, false);
            // LF_CR_* token-paste the role suffix; alpha (4th) is left untouched.
            const int role = bands >= 4 ? LF_CR_4(RED, GREEN, BLUE, UNKNOWN)
                                        : LF_CR_3(RED, GREEN, BLUE);
            // Lensfun fully corrects; lerp toward the original for partial amount.
            std::vector<float> orig;
            const float amt = m_params.vignettingAmount;
            if (amt < 0.999f)
                orig.assign(px, px + n * bands);
            mod->ApplyColorModification(px, 0.0f, 0.0f, w, h, role,
                                        w * bands * static_cast<int>(sizeof(float)));
            if (amt < 0.999f)
                for (long long i = 0; i < n * bands; ++i)
                    px[i] = orig[i] + amt * (px[i] - orig[i]);
            delete mod;
        }
    }
#endif

    // Wrap the (possibly vignetting-corrected) buffer as the working image.
    VipsImage *cur = vips_image_new_from_memory_copy(
        raw, static_cast<size_t>(w) * h * bands * sizeof(float), w, h, bands,
        VIPS_FORMAT_FLOAT);
    g_free(raw);
    if (!cur)
        return input;
    {
        VipsImage *tagged = nullptr;
        if (vips_copy(cur, &tagged, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr)) {
            g_object_unref(cur);
            return input;
        }
        g_object_unref(cur);
        cur = tagged;
    }

#ifdef LUMEN_HAVE_LENSFUN
    // --- 2. Geometry (distortion) and optional per-channel TCA ---------------
    if (doAuto && (m_params.distortion || m_params.tca)) {
        float crop = 0;
        if (const lfLens *lens = findLens(m_params, crop)) {
            int flags = 0;
            if (m_params.distortion)
                flags |= LF_MODIFY_DISTORTION;
            const bool wantTca = m_params.tca;
            if (wantTca)
                flags |= LF_MODIFY_TCA;
            auto *mod = new lfModifier(lens, crop, w, h);
            const int got = mod->Initialize(
                lens, LF_PF_F32, m_params.focalLength, m_params.aperture,
                m_params.focusDistance > 0 ? m_params.focusDistance : 1000.0f, 1.0f,
                lens->Type, flags, false);
            const float amt = m_params.distortionAmount;
            const bool tcaOn = wantTca && (got & LF_MODIFY_TCA);

            if (tcaOn) {
                // Subpixel map: 3 (R,G,B) source coords per pixel. Resample each
                // colour band through its own map; alpha follows the green map.
                std::vector<float> sub(static_cast<size_t>(n) * 2 * 3);
                if (mod->ApplySubpixelGeometryDistortion(0.0f, 0.0f, w, h, sub.data())) {
                    VipsImage *chans[4] = {nullptr, nullptr, nullptr, nullptr};
                    const int outBands = std::min(bands, 4);
                    bool ok = true;
                    for (int c = 0; c < outBands && ok; ++c) {
                        const int mapCh = std::min(c, 2); // alpha → green map
                        std::vector<float> map(static_cast<size_t>(n) * 2);
                        for (long long i = 0; i < n; ++i) {
                            float sx = sub[i * 6 + mapCh * 2 + 0];
                            float sy = sub[i * 6 + mapCh * 2 + 1];
                            const long long y = i / w, x = i % w;
                            map[i * 2 + 0] = x + amt * (sx - x);
                            map[i * 2 + 1] = y + amt * (sy - y);
                        }
                        VipsImage *band = nullptr;
                        if (vips_extract_band(cur, &band, c, nullptr)) {
                            ok = false;
                            break;
                        }
                        chans[c] = resampleThroughMap(band, map, w, h);
                        g_object_unref(band);
                        ok = chans[c] != nullptr;
                    }
                    if (ok) {
                        VipsImage *joined = nullptr;
                        if (!vips_bandjoin(chans, &joined, outBands, nullptr)) {
                            g_object_unref(cur);
                            cur = joined;
                        }
                    }
                    for (int c = 0; c < outBands; ++c)
                        if (chans[c])
                            g_object_unref(chans[c]);
                }
            } else if (m_params.distortion) {
                std::vector<float> map(static_cast<size_t>(n) * 2);
                if (mod->ApplyGeometryDistortion(0.0f, 0.0f, w, h, map.data())) {
                    if (amt < 0.999f)
                        for (long long i = 0; i < n; ++i) {
                            const long long y = i / w, x = i % w;
                            map[i * 2 + 0] = x + amt * (map[i * 2 + 0] - x);
                            map[i * 2 + 1] = y + amt * (map[i * 2 + 1] - y);
                        }
                    VipsImage *out = resampleThroughMap(cur, map, w, h);
                    if (out) {
                        g_object_unref(cur);
                        cur = out;
                    }
                }
            }
            delete mod;
        }
    }
#endif

    // --- 3. Manual perspective (homography) ---------------------------------
    if (doPersp) {
        const Mat3 H = perspectiveBackMap(w, h, m_params.keystoneV, m_params.keystoneH,
                                          m_params.rotate, m_params.scale);
        std::vector<float> map(static_cast<size_t>(n) * 2);
        for (long long i = 0; i < n; ++i) {
            const double x = static_cast<double>(i % w), y = static_cast<double>(i / w);
            const double u = H[0] * x + H[1] * y + H[2];
            const double v = H[3] * x + H[4] * y + H[5];
            const double ww = H[6] * x + H[7] * y + H[8];
            map[i * 2 + 0] = static_cast<float>(u / ww);
            map[i * 2 + 1] = static_cast<float>(v / ww);
        }
        VipsImage *out = resampleThroughMap(cur, map, w, h);
        if (out) {
            g_object_unref(cur);
            cur = out;
        }
    }

    return Image::adopt(cur);
}

QJsonObject LensCorrectionNode::saveState() const
{
    QJsonObject s = EditNode::saveState();
    s[QStringLiteral("cameraMaker")] = m_params.cameraMaker;
    s[QStringLiteral("cameraModel")] = m_params.cameraModel;
    s[QStringLiteral("lensModel")] = m_params.lensModel;
    s[QStringLiteral("focalLength")] = m_params.focalLength;
    s[QStringLiteral("aperture")] = m_params.aperture;
    s[QStringLiteral("focusDistance")] = m_params.focusDistance;
    s[QStringLiteral("cropFactor")] = m_params.cropFactor;
    s[QStringLiteral("distortion")] = m_params.distortion;
    s[QStringLiteral("distortionAmount")] = m_params.distortionAmount;
    s[QStringLiteral("tca")] = m_params.tca;
    s[QStringLiteral("vignetting")] = m_params.vignetting;
    s[QStringLiteral("vignettingAmount")] = m_params.vignettingAmount;
    s[QStringLiteral("keystoneV")] = m_params.keystoneV;
    s[QStringLiteral("keystoneH")] = m_params.keystoneH;
    s[QStringLiteral("rotate")] = m_params.rotate;
    s[QStringLiteral("scale")] = m_params.scale;
    return s;
}

void LensCorrectionNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    Params p;
    p.cameraMaker = state.value(QStringLiteral("cameraMaker")).toString();
    p.cameraModel = state.value(QStringLiteral("cameraModel")).toString();
    p.lensModel = state.value(QStringLiteral("lensModel")).toString();
    p.focalLength = static_cast<float>(state.value(QStringLiteral("focalLength")).toDouble(0));
    p.aperture = static_cast<float>(state.value(QStringLiteral("aperture")).toDouble(0));
    p.focusDistance =
        static_cast<float>(state.value(QStringLiteral("focusDistance")).toDouble(0));
    p.cropFactor = static_cast<float>(state.value(QStringLiteral("cropFactor")).toDouble(0));
    p.distortion = state.value(QStringLiteral("distortion")).toBool(true);
    p.distortionAmount =
        static_cast<float>(state.value(QStringLiteral("distortionAmount")).toDouble(1.0));
    p.tca = state.value(QStringLiteral("tca")).toBool(true);
    p.vignetting = state.value(QStringLiteral("vignetting")).toBool(true);
    p.vignettingAmount =
        static_cast<float>(state.value(QStringLiteral("vignettingAmount")).toDouble(1.0));
    p.keystoneV = static_cast<float>(state.value(QStringLiteral("keystoneV")).toDouble(0));
    p.keystoneH = static_cast<float>(state.value(QStringLiteral("keystoneH")).toDouble(0));
    p.rotate = static_cast<float>(state.value(QStringLiteral("rotate")).toDouble(0));
    p.scale = static_cast<float>(state.value(QStringLiteral("scale")).toDouble(1.0));
    setParams(p);
}
