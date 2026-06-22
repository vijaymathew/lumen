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

    // Work on an 8-bit RGBA buffer.
    VipsImage *u8 = nullptr;
    if (vips_cast(input.handle(), &u8, VIPS_FORMAT_UCHAR, nullptr))
        return input;

    size_t size = 0;
    void *buf = vips_image_write_to_memory(u8, &size);
    const int w = u8->Xsize;
    const int h = u8->Ysize;
    const int bands = u8->Bands;
    g_object_unref(u8);
    if (!buf)
        return input;

    auto *px = static_cast<uint8_t *>(buf);
    const long long n = static_cast<long long>(w) * h;
    const double t = m_intensity;
    double out[3];
    for (long long i = 0; i < n; ++i) {
        uint8_t *p = px + i * bands;
        m_lut.sample(p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, out);
        for (int c = 0; c < 3; ++c) {
            // Blend the look with the original by intensity.
            const double blended = p[c] * (1.0 - t) + out[c] * 255.0 * t;
            p[c] = static_cast<uint8_t>(std::clamp(std::lround(blended), 0L, 255L));
        }
        // alpha (and any extra band) left untouched
    }

    VipsImage *result = vips_image_new_from_memory_copy(buf, size, w, h, bands,
                                                        VIPS_FORMAT_UCHAR);
    g_free(buf);
    if (!result)
        return input;
    return Image::adopt(result);
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
