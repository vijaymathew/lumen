// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/LutNode.h"

#include <algorithm>
#include <cmath>

LutNode::LutNode()
    : EditNode(QStringLiteral("lut"))
{
}

bool LutNode::loadHald(const QString &path, QString *error)
{
    Lut3D lut = Lut3D::fromHaldFile(path, error);
    if (!lut.isValid())
        return false;
    m_lut = lut;
    m_sourcePath = path;
    invalidate();
    return true;
}

void LutNode::setLut(const Lut3D &lut)
{
    m_lut = lut;
    m_sourcePath.clear();
    invalidate();
}

void LutNode::clear()
{
    m_lut = Lut3D{};
    m_sourcePath.clear();
    invalidate();
}

void LutNode::setIntensity(float intensity)
{
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    if (intensity != m_intensity) {
        m_intensity = intensity;
        invalidate();
    }
}

void LutNode::contributeToPreview(PreviewState &state) const
{
    if (m_lut.isValid())
        state.lutIntensity = m_intensity; // last look wins (matches previewLook)
}

Image LutNode::apply(const Image &input) const
{
    if (input.isNull() || !m_lut.isValid() || m_intensity == 0.0f)
        return input;

    // Work on a float RGBA buffer (sRGB 0..255) at full precision.
    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;

    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize;
    const int h = f->Ysize;
    const int bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;

    auto *px = static_cast<float *>(buf);
    const long long n = static_cast<long long>(w) * h;
    const double t = m_intensity;
    double out[3];
    for (long long i = 0; i < n; ++i) {
        float *p = px + i * bands;
        m_lut.sample(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, out);
        for (int c = 0; c < 3; ++c)
            // Blend the look with the original by intensity.
            p[c] = static_cast<float>(p[c] * (1.0 - t) + out[c] * 255.0 * t);
        // alpha (and any extra band) left untouched
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject LutNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    if (!m_sourcePath.isEmpty())
        state[QStringLiteral("hald")] = m_sourcePath;
    state[QStringLiteral("intensity")] = m_intensity;
    return state;
}

void LutNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    const QString path = state.value(QStringLiteral("hald")).toString();
    if (path.isEmpty())
        clear();
    else
        loadHald(path);
    setIntensity(static_cast<float>(
        state.value(QStringLiteral("intensity")).toDouble(1.0)));
}
