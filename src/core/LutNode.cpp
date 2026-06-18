// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/LutNode.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

LutNode::LutNode()
    : EditNode(QStringLiteral("lut"))
{
}

bool LutNode::loadLut(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open '%1'").arg(path);
        return false;
    }
    const QByteArray bytes = file.readAll();
    const bool isCube =
        QFileInfo(path).suffix().compare(QLatin1String("cube"), Qt::CaseInsensitive) == 0;
    return loadLutData(bytes, isCube ? QStringLiteral("cube") : QStringLiteral("hald"),
                       path, error);
}

bool LutNode::loadHald(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open '%1'").arg(path);
        return false;
    }
    return loadLutData(file.readAll(), QStringLiteral("hald"), path, error);
}

bool LutNode::loadLutData(const QByteArray &bytes, const QString &kind,
                          const QString &displayPath, QString *error)
{
    Lut3D lut = kind == QLatin1String("cube") ? Lut3D::fromCubeData(bytes, error)
                                              : Lut3D::fromHaldData(bytes, error);
    if (!lut.isValid())
        return false;
    m_lut = lut;
    m_sourcePath = displayPath;
    m_sourceBytes = bytes;
    m_kind = kind;
    invalidate();
    return true;
}

void LutNode::setLut(const Lut3D &lut)
{
    m_lut = lut;
    m_sourcePath.clear();
    m_sourceBytes.clear();
    m_kind.clear();
    invalidate();
}

void LutNode::clear()
{
    m_lut = Lut3D{};
    m_sourcePath.clear();
    m_sourceBytes.clear();
    m_kind.clear();
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
        state[QStringLiteral("lut")] = m_sourcePath;
    // Embed the LUT bytes so the project is self-contained (portable, and robust
    // to the original file moving/being deleted).
    if (!m_sourceBytes.isEmpty()) {
        state[QStringLiteral("lutData")] =
            QString::fromLatin1(m_sourceBytes.toBase64());
        state[QStringLiteral("lutKind")] = m_kind;
    }
    state[QStringLiteral("intensity")] = m_intensity;
    return state;
}

void LutNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    // "lut" is the generic key; fall back to the old "hald" key for projects
    // saved before .cube support.
    QString path = state.value(QStringLiteral("lut")).toString();
    if (path.isEmpty())
        path = state.value(QStringLiteral("hald")).toString();

    const QString data64 = state.value(QStringLiteral("lutData")).toString();
    if (!data64.isEmpty()) {
        // Self-contained: rebuild from the embedded bytes (no disk access).
        QString kind = state.value(QStringLiteral("lutKind")).toString();
        if (kind.isEmpty()) // infer from the label for forward-safety
            kind = QFileInfo(path).suffix().compare(QLatin1String("cube"),
                                                    Qt::CaseInsensitive) == 0
                       ? QStringLiteral("cube")
                       : QStringLiteral("hald");
        if (!loadLutData(QByteArray::fromBase64(data64.toLatin1()), kind, path))
            clear();
    } else if (path.isEmpty()) {
        clear();
    } else {
        loadLut(path); // legacy project: load from the referenced file
    }
    setIntensity(static_cast<float>(
        state.value(QStringLiteral("intensity")).toDouble(1.0)));
}
