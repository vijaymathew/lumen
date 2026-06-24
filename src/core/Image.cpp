// vips (and the glib headers it pulls in) MUST be included before any Qt header
// — glib uses `signals` as an identifier and Qt defines it as a macro. Image.h
// only references QString, but keep this ordering to be safe.
#include <vips/vips.h>

#include "core/Image.h"

#include <QFileInfo>

#include <algorithm>

namespace {

// Returns a new sRGB, `fmt`-format, 4-band (RGBA) image the caller owns, or
// nullptr on error. Does not consume `in`. `fmt` is UCHAR for display/export or
// FLOAT for the working pipeline (sRGB-encoded float, 0..255, unclamped so
// highlights >255 survive — the headroom that makes RAW useful).
VipsImage *toRGBA(VipsImage *in, VipsBandFormat fmt)
{
    VipsImage *cur = in;
    g_object_ref(cur); // own a ref to whatever cur points at
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };

    VipsImage *t = nullptr;
    if (vips_colourspace(cur, &t, VIPS_INTERPRETATION_sRGB, nullptr)) {
        g_object_unref(cur);
        return nullptr;
    }
    replace(t);

    if (cur->BandFmt != fmt) {
        t = nullptr;
        if (vips_cast(cur, &t, fmt, nullptr)) {
            g_object_unref(cur);
            return nullptr;
        }
        replace(t);
    }

    if (!vips_image_hasalpha(cur)) {
        t = nullptr;
        if (vips_addalpha(cur, &t, nullptr)) {
            g_object_unref(cur);
            return nullptr;
        }
        replace(t);
    }

    return cur; // caller owns
}

// Display/export form: 8-bit sRGB RGBA.
VipsImage *toDisplayRGBA(VipsImage *in) { return toRGBA(in, VIPS_FORMAT_UCHAR); }
// Working form: float sRGB RGBA (the edit pipeline's currency).
VipsImage *toWorkingRGBA(VipsImage *in) { return toRGBA(in, VIPS_FORMAT_FLOAT); }

} // namespace

Image::Image(_VipsImage *img)
    : m_image(img)
{
    if (m_image)
        g_object_ref(m_image);
}

Image Image::adopt(_VipsImage *img)
{
    Image i;
    i.m_image = img; // takes the caller's existing reference, no extra ref
    return i;
}

Image Image::black(int width, int height)
{
    VipsImage *out = nullptr;
    if (vips_black(&out, width, height, nullptr))
        return Image();
    return Image::adopt(out);
}

Image::Image(const Image &other)
    : m_image(other.m_image)
{
    if (m_image)
        g_object_ref(m_image);
}

Image &Image::operator=(const Image &other)
{
    if (this != &other) {
        if (other.m_image)
            g_object_ref(other.m_image);
        if (m_image)
            g_object_unref(m_image);
        m_image = other.m_image;
    }
    return *this;
}

Image::Image(Image &&other) noexcept
    : m_image(other.m_image)
{
    other.m_image = nullptr;
}

Image &Image::operator=(Image &&other) noexcept
{
    if (this != &other) {
        if (m_image)
            g_object_unref(m_image);
        m_image = other.m_image;
        other.m_image = nullptr;
    }
    return *this;
}

Image::~Image()
{
    if (m_image)
        g_object_unref(m_image);
}

int Image::width() const
{
    return m_image ? m_image->Xsize : 0;
}

int Image::height() const
{
    return m_image ? m_image->Ysize : 0;
}

Image Image::fromInterleaved(const void *data, int width, int height, int bands)
{
    if (!data || width <= 0 || height <= 0 || bands < 1)
        return Image();

    VipsImage *raw = vips_image_new_from_memory_copy(
        data, static_cast<size_t>(width) * height * bands, width, height, bands,
        VIPS_FORMAT_UCHAR);
    if (!raw)
        return Image();

    // new_from_memory guesses interpretation from band count (4 bands ->
    // MULTIBAND), which would make a later vips_colourspace(->sRGB) scramble the
    // channels. Tag it sRGB so the buffer round-trips faithfully.
    VipsImage *tagged = nullptr;
    if (vips_copy(raw, &tagged, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr)) {
        g_object_unref(raw);
        return Image();
    }
    g_object_unref(raw);

    // Promote to the float working format so 8-bit-buffer producers (heal, etc.)
    // stay in the high-precision pipeline.
    VipsImage *f = nullptr;
    if (vips_cast(tagged, &f, VIPS_FORMAT_FLOAT, nullptr)) {
        g_object_unref(tagged);
        return Image();
    }
    g_object_unref(tagged);
    return Image::adopt(f);
}

Image Image::fromInterleavedFloat(const float *data, int width, int height, int bands)
{
    if (!data || width <= 0 || height <= 0 || bands < 1)
        return Image();

    VipsImage *raw = vips_image_new_from_memory_copy(
        data, static_cast<size_t>(width) * height * bands * sizeof(float), width,
        height, bands, VIPS_FORMAT_FLOAT);
    if (!raw)
        return Image();

    VipsImage *tagged = nullptr; // tag sRGB (see fromInterleaved)
    if (vips_copy(raw, &tagged, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr)) {
        g_object_unref(raw);
        return Image();
    }
    g_object_unref(raw);
    return Image::adopt(tagged);
}

Image Image::fromFile(const QString &path, QString *error)
{
    const QByteArray utf8 = path.toUtf8();
    VipsImage *img = vips_image_new_from_file(utf8.constData(), nullptr);
    if (!img) {
        if (error)
            *error = QStringLiteral("Could not open '%1': %2")
                         .arg(path, QString::fromUtf8(vips_error_buffer()));
        vips_error_clear();
        return Image();
    }

    VipsImage *norm = toWorkingRGBA(img);
    g_object_unref(img);
    if (!norm) {
        if (error)
            *error = QStringLiteral("Could not decode '%1': %2")
                         .arg(path, QString::fromUtf8(vips_error_buffer()));
        vips_error_clear();
        return Image();
    }
    return Image::adopt(norm);
}

Image Image::fromBytes(const void *data, qsizetype size, QString *error)
{
    if (!data || size <= 0) {
        if (error)
            *error = QStringLiteral("Empty image data");
        return Image();
    }
    VipsImage *img =
        vips_image_new_from_buffer(data, static_cast<size_t>(size), "", nullptr);
    if (!img) {
        if (error)
            *error = QStringLiteral("Could not decode image: %1")
                         .arg(QString::fromUtf8(vips_error_buffer()));
        vips_error_clear();
        return Image();
    }
    VipsImage *norm = toWorkingRGBA(img); // float sRGB RGBA
    g_object_unref(img);
    if (!norm) {
        if (error)
            *error = QStringLiteral("Could not normalise image: %1")
                         .arg(QString::fromUtf8(vips_error_buffer()));
        vips_error_clear();
        return Image();
    }
    // Materialise into an independent memory image so the result no longer
    // references `data` (which the caller may free immediately).
    const int w = vips_image_get_width(norm);
    const int h = vips_image_get_height(norm);
    const int bands = vips_image_get_bands(norm);
    void *buf = vips_image_write_to_memory(norm, nullptr);
    g_object_unref(norm);
    if (!buf) {
        if (error)
            *error = QStringLiteral("Could not materialise image");
        return Image();
    }
    Image result = Image::fromInterleavedFloat(static_cast<float *>(buf), w, h, bands);
    g_free(buf);
    return result;
}

QImage Image::toQImage() const
{
    if (!m_image)
        return QImage();

    VipsImage *norm = toDisplayRGBA(m_image);
    if (!norm) {
        vips_error_clear();
        return QImage();
    }

    const int w = norm->Xsize;
    const int h = norm->Ysize;
    size_t size = 0;
    void *buf = vips_image_write_to_memory(norm, &size);
    g_object_unref(norm);
    if (!buf) {
        vips_error_clear();
        return QImage();
    }

    // QImage takes ownership of the g_malloc'd buffer (freed via g_free).
    return QImage(static_cast<uchar *>(buf), w, h, w * 4,
                  QImage::Format_RGBA8888,
                  [](void *p) { g_free(p); }, buf);
}

bool Image::saveToFile(const QString &path, int quality, int bits, QString *error) const
{
    if (!m_image) {
        if (error)
            *error = QStringLiteral("No image to export");
        return false;
    }

    // libvips chooses the saver from the extension and parses per-format options
    // appended as filename[opt=val]. Apply quality to the lossy savers only.
    const QString suffix = QFileInfo(path).suffix().toLower();
    QString target = path;
    if (quality >= 0
        && (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
            || suffix == QLatin1String("webp")))
        target = QStringLiteral("%1[Q=%2]").arg(path).arg(std::clamp(quality, 0, 100));
    else if (bits == 16 && suffix == QLatin1String("png"))
        // Force a 16-bit PNG; pngsave otherwise reduces low-information images.
        target = QStringLiteral("%1[bitdepth=16]").arg(path);
    const QByteArray utf8 = target.toUtf8();

    VipsImage *cur = m_image; // float sRGB working image (0..255)
    g_object_ref(cur);
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };
    const auto fail = [&](const QString &msg) {
        if (error)
            *error = msg.isEmpty() ? QString::fromUtf8(vips_error_buffer()) : msg;
        vips_error_clear();
        g_object_unref(cur);
        return false;
    };

    // Quantise the float working image to the requested output depth. 16-bit
    // scales 0..255 → 0..65535 (×257) before the USHORT cast.
    VipsImage *q = nullptr;
    if (bits == 16) {
        VipsImage *scaled = nullptr;
        if (vips_linear1(cur, &scaled, 257.0, 0.0, nullptr))
            return fail({});
        replace(scaled);
        if (vips_cast(cur, &q, VIPS_FORMAT_USHORT, nullptr))
            return fail({});
    } else if (vips_cast(cur, &q, VIPS_FORMAT_UCHAR, nullptr)) {
        return fail({});
    }
    replace(q);

    // Drop a trailing alpha band so formats without alpha (e.g. JPEG) succeed.
    if (vips_image_hasalpha(cur)) {
        VipsImage *rgb = nullptr;
        const int n = vips_image_get_bands(cur) - 1;
        if (vips_extract_band(cur, &rgb, 0, "n", n, nullptr))
            return fail({});
        replace(rgb);
    }

    const int rc = vips_image_write_to_file(cur, utf8.constData(), nullptr);
    g_object_unref(cur);
    if (rc) {
        if (error)
            *error = QString::fromUtf8(vips_error_buffer());
        vips_error_clear();
        return false;
    }
    return true;
}
