// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/Vignette.h"

#include <algorithm>
#include <cmath>

bool VignetteParams::isIdentity() const
{
    return !enabled || std::abs(amount) < 1e-4f;
}

void VignetteParams::sanitize()
{
    amount = std::clamp(amount, -100.0f, 100.0f);
    midpoint = std::clamp(midpoint, 0.0f, 100.0f);
    roundness = std::clamp(roundness, -100.0f, 100.0f);
    feather = std::clamp(feather, 0.0f, 100.0f);
}

QJsonObject VignetteParams::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("enabled")] = enabled;
    o[QStringLiteral("amount")] = amount;
    o[QStringLiteral("midpoint")] = midpoint;
    o[QStringLiteral("roundness")] = roundness;
    o[QStringLiteral("feather")] = feather;
    return o;
}

VignetteParams VignetteParams::fromJson(const QJsonObject &o)
{
    VignetteParams v;
    v.enabled = o.value(QStringLiteral("enabled")).toBool(false);
    v.amount = static_cast<float>(o.value(QStringLiteral("amount")).toDouble(0.0));
    v.midpoint = static_cast<float>(o.value(QStringLiteral("midpoint")).toDouble(50.0));
    v.roundness = static_cast<float>(o.value(QStringLiteral("roundness")).toDouble(0.0));
    v.feather = static_cast<float>(o.value(QStringLiteral("feather")).toDouble(50.0));
    v.sanitize();
    return v;
}

namespace {
float smoothstep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

Image applyVignette(const Image &img, const VignetteParams &vIn)
{
    if (img.isNull() || vIn.isIdentity())
        return img;
    VignetteParams v = vIn;
    v.sanitize();

    VipsImage *f = nullptr;
    if (vips_cast(img.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return img;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize, h = f->Ysize, bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return img;

    // --- Parameters shared verbatim with present.frag (see docs/VIGNETTE.md) ---
    const float A = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    const float ax = A >= 1.0f ? A : 1.0f;        // stretch the longer axis so the
    const float ay = A < 1.0f ? 1.0f / A : 1.0f;  //   falloff hugs the frame
    const float n = 3.0f - v.roundness / 100.0f;  // shape: 2=circle … 4=boxy
    const float m = v.midpoint / 100.0f;
    const float feather = std::max(v.feather / 100.0f, 1e-3f);
    const float k = v.amount / 100.0f;
    const int colorBands = std::min(bands, 3);

    auto *px = static_cast<float *>(buf);
    for (int y = 0; y < h; ++y) {
        const float cy = ((y + 0.5f) / h - 0.5f) * 2.0f * ay;
        for (int x = 0; x < w; ++x) {
            const float cx = ((x + 0.5f) / w - 0.5f) * 2.0f * ax;
            const float d = std::pow(std::pow(std::abs(cx), n) + std::pow(std::abs(cy), n),
                                     1.0f / n);
            const float t = smoothstep(m, m + feather, d);
            const float gain = std::clamp(1.0f + k * t, 0.0f, 4.0f);
            float *p = px + (static_cast<size_t>(y) * w + x) * bands;
            for (int c = 0; c < colorBands; ++c)
                p[c] *= gain; // alpha / extra bands untouched
        }
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? img : result;
}
