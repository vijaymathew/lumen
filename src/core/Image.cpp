// vips (and the glib headers it pulls in) MUST be included before any Qt header
// — glib uses `signals` as an identifier and Qt defines it as a macro. Image.h
// only references QString, but keep this ordering to be safe.
#include <vips/vips.h>

#include "core/Image.h"

namespace {

// Returns a new sRGB / 8-bit / 4-band (RGBA) image that the caller owns, or
// nullptr on error. Does not consume `in`.
VipsImage *toDisplayRGBA(VipsImage *in)
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

    if (cur->BandFmt != VIPS_FORMAT_UCHAR) {
        t = nullptr;
        if (vips_cast(cur, &t, VIPS_FORMAT_UCHAR, nullptr)) {
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

    VipsImage *norm = toDisplayRGBA(img);
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

bool Image::saveToFile(const QString &path, QString *error) const
{
    if (!m_image) {
        if (error)
            *error = QStringLiteral("No image to export");
        return false;
    }

    const QByteArray utf8 = path.toUtf8();

    // Drop a trailing alpha band so formats without alpha (e.g. JPEG) succeed.
    VipsImage *out = m_image;
    bool owned = false;
    if (vips_image_hasalpha(m_image)) {
        VipsImage *rgb = nullptr;
        const int n = vips_image_get_bands(m_image) - 1;
        if (vips_extract_band(m_image, &rgb, 0, "n", n, nullptr)) {
            if (error)
                *error = QString::fromUtf8(vips_error_buffer());
            vips_error_clear();
            return false;
        }
        out = rgb;
        owned = true;
    }

    const int rc = vips_image_write_to_file(out, utf8.constData(), nullptr);
    if (owned)
        g_object_unref(out);
    if (rc) {
        if (error)
            *error = QString::fromUtf8(vips_error_buffer());
        vips_error_clear();
        return false;
    }
    return true;
}
