// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/GrainNode.h"

#include <algorithm>
#include <cmath>

namespace {
// Hash of an integer lattice point → [0,1]. Mirrored verbatim in texture.frag.
float hash2(float x, float y)
{
    const float h = x * 127.1f + y * 311.7f;
    const float s = std::sin(h) * 43758.5453123f;
    return s - std::floor(s); // fract
}
} // namespace

float GrainNode::valueNoise(float x, float y)
{
    const float ix = std::floor(x), iy = std::floor(y);
    const float fx = x - ix, fy = y - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx); // smoothstep weights
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash2(ix, iy);
    const float b = hash2(ix + 1.0f, iy);
    const float c = hash2(ix, iy + 1.0f);
    const float d = hash2(ix + 1.0f, iy + 1.0f);
    const float top = a + (b - a) * ux;
    const float bot = c + (d - c) * ux;
    return top + (bot - top) * uy;
}

GrainNode::GrainNode()
    : EditNode(QStringLiteral("grain"))
{
}

void GrainNode::setValues(const Values &values)
{
    Values v = values;
    v.amount = std::clamp(v.amount, 0.0f, 100.0f);
    v.size = std::clamp(v.size, kMinSize, kMaxSize);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

void GrainNode::contributeToPreview(PreviewState &state) const
{
    if (!m_values.enabled || m_values.amount <= 0.0f)
        return; // passthrough → leave grainAmount at its 0 default
    state.grainAmount = m_values.amount / 100.0f;
    state.grainSize = m_values.size;
}

Image GrainNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled || m_values.amount <= 0.0f)
        return input;

    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize, h = f->Ysize, bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;

    const float size = std::max(m_values.size, kMinSize);
    const float gain = (m_values.amount / 100.0f) * kStrength;
    const int colorBands = std::min(bands, 3);

    auto *px = static_cast<float *>(buf);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // Pixel coordinate → noise field (matches texture.frag's gl_FragCoord).
            const float n = valueNoise(x / size + kSeed, y / size + kSeed);
            const float delta = (n - 0.5f) * gain * 255.0f; // monochrome
            float *p = px + (static_cast<size_t>(y) * w + x) * bands;
            for (int c = 0; c < colorBands; ++c)
                p[c] += delta; // alpha / extra bands untouched
        }
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject GrainNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("grainEnabled")] = m_values.enabled;
    state[QStringLiteral("amount")] = m_values.amount;
    state[QStringLiteral("size")] = m_values.size;
    return state;
}

void GrainNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    Values v;
    v.enabled = state.value(QStringLiteral("grainEnabled")).toBool(false);
    v.amount = static_cast<float>(state.value(QStringLiteral("amount")).toDouble(25.0));
    v.size = static_cast<float>(state.value(QStringLiteral("size")).toDouble(2.0));
    setValues(v);
}
