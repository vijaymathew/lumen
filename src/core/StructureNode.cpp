// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/StructureNode.h"

#include <algorithm>
#include <cmath>
#include <vector>

StructureNode::StructureNode()
    : EditNode(QStringLiteral("structure"))
{
}

void StructureNode::setValues(const Values &values)
{
    Values v = values;
    v.amount = std::clamp(v.amount, kMinAmount, kMaxAmount);
    v.radius = std::clamp(v.radius, kMinRadius, kMaxRadius);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

Image StructureNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled || m_values.amount == 0.0f)
        return input;

    // Pull the working image into a float RGB(A) buffer (0..255, sRGB-tagged) —
    // the same convention GrainNode/SharpenNode operate in.
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize, h = f->Ysize, bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;
    if (bands < 1) {
        g_free(buf);
        return input;
    }

    auto *px = static_cast<float *>(buf);
    const int colorBands = std::min(bands, 3);
    const size_t count = static_cast<size_t>(w) * h;

    // Rec.709 luminance of the colour bands (achromatic, so the boost adds no
    // saturation). A single-band image scales the mono greyscale case too.
    static constexpr float kLumW[3] = {0.2126f, 0.7152f, 0.0722f};
    std::vector<float> lum(count);
    for (size_t i = 0; i < count; ++i) {
        const float *p = px + i * bands;
        if (colorBands == 1) {
            lum[i] = p[0];
        } else {
            float s = 0.0f;
            for (int c = 0; c < colorBands; ++c)
                s += kLumW[c] * p[c];
            lum[i] = s;
        }
    }

    // Blur the luminance at the structure scale (large sigma = medium-scale local
    // contrast, not fine edges). vips_gaussblur runs the heavy kernel; the buffer
    // must outlive the readback, so keep `lum` alive until we've copied `lblur`.
    VipsImage *limg = vips_image_new_from_memory(lum.data(), count * sizeof(float), w, h, 1,
                                                 VIPS_FORMAT_FLOAT);
    if (!limg) {
        g_free(buf);
        return input;
    }
    VipsImage *lblurImg = nullptr;
    if (vips_gaussblur(limg, &lblurImg, static_cast<double>(m_values.radius), nullptr)) {
        g_object_unref(limg);
        g_free(buf);
        return input;
    }
    void *lblurBuf = vips_image_write_to_memory(lblurImg, nullptr);
    g_object_unref(lblurImg);
    g_object_unref(limg);
    if (!lblurBuf) {
        g_free(buf);
        return input;
    }
    const auto *lblur = static_cast<const float *>(lblurBuf);

    // out = in + k · w(L) · (L − blur(L)). The midtone weight w peaks at mid-grey
    // and falls to 0 at the tonal extremes, so structure lands on texture rather
    // than crushing shadows / blowing highlights (and suppresses edge halos).
    const float k = m_values.amount / 100.0f;
    for (size_t i = 0; i < count; ++i) {
        const float detail = lum[i] - lblur[i];
        const float t = std::clamp(lum[i] / 255.0f, 0.0f, 1.0f);
        const float wmid = 1.0f - std::fabs(2.0f * t - 1.0f); // triangular, peak at 0.5
        const float delta = k * wmid * detail;
        float *p = px + i * bands;
        for (int c = 0; c < colorBands; ++c)
            p[c] += delta; // alpha / extra bands untouched
    }
    g_free(lblurBuf);

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject StructureNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("structureEnabled")] = m_values.enabled;
    state[QStringLiteral("amount")] = m_values.amount;
    state[QStringLiteral("radius")] = m_values.radius;
    return state;
}

void StructureNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    Values v;
    v.enabled = state.value(QStringLiteral("structureEnabled")).toBool(false);
    v.amount = static_cast<float>(state.value(QStringLiteral("amount")).toDouble(40.0));
    v.radius = static_cast<float>(state.value(QStringLiteral("radius")).toDouble(12.0));
    setValues(v);
}
