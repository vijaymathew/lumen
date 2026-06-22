// vips (and the glib/gio headers it pulls in) MUST be included before any Qt
// header: glib uses `signals` as an identifier and Qt defines it as a macro, so
// parsing glib first avoids the collision. Do not reorder these includes.
#include <vips/vips.h>

#include "core/ImageBuffer.h"

bool ImageBuffer::initLibrary(const char *argv0)
{
    // VIPS_INIT returns non-zero on failure.
    return VIPS_INIT(argv0) == 0;
}

void ImageBuffer::shutdownLibrary()
{
    vips_shutdown();
}

bool ImageBuffer::load(const QString &path)
{
    m_error.clear();
    m_image = QImage();

    const QByteArray utf8 = path.toUtf8();

    // Decode. "access=sequential" would be faster for one-shot reads, but we
    // want random access for an interactive viewer, so use the default.
    VipsImage *img = vips_image_new_from_file(utf8.constData(), nullptr);
    if (!img) {
        m_error = QStringLiteral("Could not open '%1': %2")
                      .arg(path, QString::fromUtf8(vips_error_buffer()));
        vips_error_clear();
        return false;
    }

    // We chain conversions, unref-ing each intermediate. `cur` always holds the
    // image we still own.
    VipsImage *cur = img;
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };

    // 1. Normalise to 8-bit sRGB so the upload format is predictable.
    {
        VipsImage *srgb = nullptr;
        if (vips_colourspace(cur, &srgb, VIPS_INTERPRETATION_sRGB, nullptr)) {
            m_error = QStringLiteral("Colourspace conversion failed: %1")
                          .arg(QString::fromUtf8(vips_error_buffer()));
            vips_error_clear();
            g_object_unref(cur);
            return false;
        }
        replace(srgb);
    }

    // 2. Cast to uchar in case the source was higher bit-depth.
    if (cur->BandFmt != VIPS_FORMAT_UCHAR) {
        VipsImage *casted = nullptr;
        if (vips_cast(cur, &casted, VIPS_FORMAT_UCHAR, nullptr)) {
            m_error = QStringLiteral("Cast to 8-bit failed: %1")
                          .arg(QString::fromUtf8(vips_error_buffer()));
            vips_error_clear();
            g_object_unref(cur);
            return false;
        }
        replace(casted);
    }

    // 3. Guarantee a 4th (alpha) band so the texture is tightly packed RGBA8.
    if (!vips_image_hasalpha(cur)) {
        VipsImage *withAlpha = nullptr;
        if (vips_addalpha(cur, &withAlpha, nullptr)) {
            m_error = QStringLiteral("Adding alpha channel failed: %1")
                          .arg(QString::fromUtf8(vips_error_buffer()));
            vips_error_clear();
            g_object_unref(cur);
            return false;
        }
        replace(withAlpha);
    }

    const int w = cur->Xsize;
    const int h = cur->Ysize;

    // 4. Materialise to a contiguous RGBA8 buffer. vips hands us a g_malloc'd
    //    block; QImage takes ownership and frees it with g_free on destruction.
    size_t size = 0;
    void *buf = vips_image_write_to_memory(cur, &size);
    g_object_unref(cur);
    if (!buf) {
        m_error = QStringLiteral("Reading pixels failed: %1")
                      .arg(QString::fromUtf8(vips_error_buffer()));
        vips_error_clear();
        return false;
    }

    // vips packs bands tightly: stride == width * 4 for RGBA8.
    m_image = QImage(static_cast<uchar *>(buf), w, h, w * 4,
                     QImage::Format_RGBA8888,
                     [](void *p) { g_free(p); }, buf);

    if (m_image.isNull()) {
        m_error = QStringLiteral("Failed to wrap decoded pixels");
        return false;
    }
    return true;
}
