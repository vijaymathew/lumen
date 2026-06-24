#include "core/RawLoader.h"

#include <QFileInfo>

#include <libraw/libraw.h>

#include <cstdint>
#include <vector>

namespace {

// Configure LibRaw for a viewable 8-bit sRGB result (camera white balance), then
// demosaic and hand back an Image. Assumes the file/buffer is already opened.
Image processToImage(LibRaw &raw, QString *error)
{
    raw.imgdata.params.output_bps = 16;    // 16-bit per channel → float pipeline
    raw.imgdata.params.output_color = 1;   // sRGB
    raw.imgdata.params.use_camera_wb = 1;  // as-shot white balance

    if (const int e = raw.unpack()) {
        if (error)
            *error = QStringLiteral("RAW unpack failed: %1")
                         .arg(QString::fromUtf8(LibRaw::strerror(e)));
        return Image();
    }
    if (const int e = raw.dcraw_process()) {
        if (error)
            *error = QStringLiteral("RAW process failed: %1")
                         .arg(QString::fromUtf8(LibRaw::strerror(e)));
        return Image();
    }

    int e = 0;
    libraw_processed_image_t *img = raw.dcraw_make_mem_image(&e);
    if (!img) {
        if (error)
            *error = QStringLiteral("RAW render failed: %1")
                         .arg(QString::fromUtf8(LibRaw::strerror(e)));
        return Image();
    }
    if (img->type != LIBRAW_IMAGE_BITMAP || img->bits != 16 || img->colors != 3) {
        LibRaw::dcraw_clear_mem(img);
        if (error)
            *error = QStringLiteral("Unexpected RAW output format");
        return Image();
    }

    // 16-bit sRGB (0..65535) → float RGBA in the working scale (0..255 sRGB; ÷257
    // maps 65535→255), preserving the full RAW precision. Alpha opaque.
    const int w = img->width, h = img->height;
    std::vector<float> rgba(static_cast<size_t>(w) * h * 4);
    const auto *src = reinterpret_cast<const uint16_t *>(img->data);
    for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i) {
        rgba[i * 4 + 0] = src[i * 3 + 0] / 257.0f;
        rgba[i * 4 + 1] = src[i * 3 + 1] / 257.0f;
        rgba[i * 4 + 2] = src[i * 3 + 2] / 257.0f;
        rgba[i * 4 + 3] = 255.0f;
    }
    LibRaw::dcraw_clear_mem(img);
    return Image::fromInterleavedFloat(rgba.data(), w, h, 4);
}

} // namespace

const QStringList &raw::extensions()
{
    static const QStringList kExts = {
        QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("nef"),
        QStringLiteral("arw"), QStringLiteral("dng"), QStringLiteral("raf"),
        QStringLiteral("orf"), QStringLiteral("rw2"), QStringLiteral("pef"),
        QStringLiteral("srw"), QStringLiteral("nrw"), QStringLiteral("3fr"),
        QStringLiteral("dcr"), QStringLiteral("kdc"), QStringLiteral("mrw"),
        QStringLiteral("x3f"), QStringLiteral("erf"), QStringLiteral("raw")};
    return kExts;
}

bool raw::isRawPath(const QString &path)
{
    return extensions().contains(QFileInfo(path).suffix().toLower());
}

Image raw::decodeFile(const QString &path, QString *error)
{
    LibRaw raw;
    if (const int e = raw.open_file(path.toUtf8().constData())) {
        if (error)
            *error = QStringLiteral("Could not open RAW '%1': %2")
                         .arg(path, QString::fromUtf8(LibRaw::strerror(e)));
        return Image();
    }
    return processToImage(raw, error);
}

Image raw::decodeBytes(const void *data, qsizetype size, QString *error)
{
    if (!data || size <= 0) {
        if (error)
            *error = QStringLiteral("Empty RAW data");
        return Image();
    }
    LibRaw raw;
    if (const int e = raw.open_buffer(const_cast<void *>(data), static_cast<size_t>(size))) {
        if (error)
            *error = QStringLiteral("Could not read RAW data: %1")
                         .arg(QString::fromUtf8(LibRaw::strerror(e)));
        return Image();
    }
    return processToImage(raw, error);
}
