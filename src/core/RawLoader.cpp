#include "core/RawLoader.h"

#include <QFileInfo>
#include <QImage>
#include <QTransform>

#include <libraw/libraw.h>

#include <cstdint>
#include <vector>

namespace {

// Pulls the camera/lens identity LibRaw parsed from EXIF (valid after open).
void fillMetadata(LibRaw &raw, raw::LensMetadata *meta)
{
    if (!meta)
        return;
    const auto &id = raw.imgdata.idata;
    const auto &lens = raw.imgdata.lens;
    const auto &other = raw.imgdata.other;
    meta->cameraMaker = QString::fromUtf8(id.make).trimmed();
    meta->cameraModel = QString::fromUtf8(id.model).trimmed();
    // Prefer the EXIF lens name; fall back to the maker-notes lens string.
    meta->lensModel = QString::fromUtf8(lens.Lens).trimmed();
    if (meta->lensModel.isEmpty())
        meta->lensModel = QString::fromUtf8(lens.makernotes.Lens).trimmed();
    meta->focalLength = static_cast<float>(other.focal_len);
    meta->aperture = static_cast<float>(other.aperture);
    // The focus distance at capture isn't reliably exposed by LibRaw; leave it
    // unknown (0) so the correction assumes ≈∞, which is right for landscapes.
    meta->focusDistance = 0.0f;
}

// Captures the camera colour matrices LibRaw computes (valid after dcraw_process)
// for the camera-accurate white balance. rgb_cam is camera→sRGB (3x4, the 4th
// column is the second green and is dropped); cam_xyz is XYZ→camera (4x3, drop
// the 4th row); cam_mul are the as-shot multipliers.
void fillColorProfile(LibRaw &raw, raw::LensMetadata *meta)
{
    if (!meta)
        return;
    const auto &col = raw.imgdata.color;
    raw::ColorProfile &p = meta->color;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            p.camToRgb[r * 3 + c] = col.rgb_cam[r][c];
            p.xyzToCam[r * 3 + c] = col.cam_xyz[r][c];
        }
    }
    for (int c = 0; c < 3; ++c)
        p.asShotMul[c] = col.cam_mul[c];
    // A real profile has a non-trivial camera→sRGB matrix and positive as-shot
    // green; otherwise leave `valid` false so WB uses the sRGB fallback.
    const bool hasMatrix = col.rgb_cam[0][0] != 0.0f || col.rgb_cam[0][1] != 0.0f
                        || col.rgb_cam[1][1] != 0.0f;
    p.valid = hasMatrix && p.asShotMul[1] > 0.0;
}

// Configure LibRaw for a viewable 16-bit sRGB result, then demosaic and hand back
// an Image. `opts` drives the automatic decode-time adjustments. Assumes the
// file/buffer is already opened.
Image processToImage(LibRaw &raw, QString *error, raw::LensMetadata *meta,
                     const raw::RawDecodeOptions &opts)
{
    auto &p = raw.imgdata.params;
    p.output_bps = 16;    // 16-bit per channel → float pipeline
    p.output_color = 1;   // sRGB
    // Automatic brightness (LibRaw scales exposure, clipping `auto_bright_thr`).
    p.no_auto_bright = opts.autoBright ? 0 : 1;
    p.auto_bright_thr = opts.autoBrightThreshold;
    // Highlight handling: 0 clip, 1 unclip, 2 blend, 3+ reconstruct.
    p.highlight = opts.highlight;
    // White balance source.
    p.use_camera_wb = (opts.wb == raw::RawDecodeOptions::Camera) ? 1 : 0;
    p.use_auto_wb = (opts.wb == raw::RawDecodeOptions::Auto) ? 1 : 0;
    // Demosaic algorithm.
    p.user_qual = opts.demosaic;

    fillMetadata(raw, meta);

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

    // Colour matrices are populated by dcraw_process — capture them for WB.
    fillColorProfile(raw, meta);

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

Image raw::decodeFile(const QString &path, QString *error, LensMetadata *meta,
                      const RawDecodeOptions &opts)
{
    LibRaw raw;
    if (const int e = raw.open_file(path.toUtf8().constData())) {
        if (error)
            *error = QStringLiteral("Could not open RAW '%1': %2")
                         .arg(path, QString::fromUtf8(LibRaw::strerror(e)));
        return Image();
    }
    return processToImage(raw, error, meta, opts);
}

Image raw::decodeBytes(const void *data, qsizetype size, QString *error, LensMetadata *meta,
                       const RawDecodeOptions &opts)
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
    return processToImage(raw, error, meta, opts);
}

namespace {

// The camera's embedded preview JPEG (or, rarely, an RGB bitmap), rotated to the
// shooting orientation. Fast — no demosaic. Null QImage if the file has no
// embedded preview. Assumes `raw` has an opened file.
QImage embeddedThumb(LibRaw &raw)
{
    if (raw.unpack_thumb() != LIBRAW_SUCCESS)
        return QImage();
    int err = 0;
    libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&err);
    if (!thumb)
        return QImage();

    QImage img;
    if (thumb->type == LIBRAW_IMAGE_JPEG) {
        img.loadFromData(reinterpret_cast<const uchar *>(thumb->data), thumb->data_size,
                         "JPEG");
    } else if (thumb->type == LIBRAW_IMAGE_BITMAP && thumb->colors == 3
               && thumb->bits == 8) {
        const QImage view(reinterpret_cast<const uchar *>(thumb->data), thumb->width,
                          thumb->height, thumb->width * 3, QImage::Format_RGB888);
        img = view.copy(); // detach from LibRaw's buffer before it is freed
    }
    const int flip = raw.imgdata.sizes.flip; // capture orientation (dcraw codes)
    LibRaw::dcraw_clear_mem(thumb);

    // The embedded preview is stored in sensor orientation; rotate to display
    // orientation (dcraw flip: 3=180°, 5=90° CCW, 6=90° CW).
    if (!img.isNull() && (flip == 3 || flip == 5 || flip == 6)) {
        QTransform t;
        t.rotate(flip == 3 ? 180 : (flip == 6 ? 90 : 270));
        img = img.transformed(t, Qt::SmoothTransformation);
    }
    return img;
}

// Fallback for files with no embedded preview (e.g. some DNGs): a half-size
// demosaic — a quarter of the pixels, so far quicker than a full decode while
// still viewable. dcraw_process already applies the capture orientation.
QImage halfSizePreview(LibRaw &raw)
{
    auto &p = raw.imgdata.params;
    p.half_size = 1;    // quarter-resolution: fast
    p.output_bps = 8;   // 8-bit is plenty for a thumbnail
    p.output_color = 1; // sRGB
    p.use_camera_wb = 1;
    if (raw.unpack() != LIBRAW_SUCCESS || raw.dcraw_process() != LIBRAW_SUCCESS)
        return QImage();
    int err = 0;
    libraw_processed_image_t *img = raw.dcraw_make_mem_image(&err);
    if (!img)
        return QImage();
    QImage out;
    if (img->type == LIBRAW_IMAGE_BITMAP && img->colors == 3 && img->bits == 8) {
        const QImage view(reinterpret_cast<const uchar *>(img->data), img->width,
                          img->height, img->width * 3, QImage::Format_RGB888);
        out = view.copy();
    }
    LibRaw::dcraw_clear_mem(img);
    return out;
}

} // namespace

QImage raw::loadThumbnail(const QString &path, int maxEdge, QString *error)
{
    LibRaw raw;
    if (const int e = raw.open_file(path.toUtf8().constData())) {
        if (error)
            *error = QStringLiteral("Could not open '%1': %2")
                         .arg(path, QString::fromUtf8(LibRaw::strerror(e)));
        return QImage();
    }

    // Prefer the embedded preview; fall back to a quick half-size decode so every
    // decodable RAW yields a thumbnail, even those without an embedded preview.
    QImage img = embeddedThumb(raw);
    if (img.isNull())
        img = halfSizePreview(raw);
    if (img.isNull()) {
        if (error)
            *error = QStringLiteral("No preview available for '%1'").arg(path);
        return QImage();
    }

    if (maxEdge > 0 && (img.width() > maxEdge || img.height() > maxEdge))
        img = img.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QJsonObject raw::RawDecodeOptions::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("autoBright")] = autoBright;
    o[QStringLiteral("autoBrightThreshold")] = autoBrightThreshold;
    o[QStringLiteral("highlight")] = highlight;
    o[QStringLiteral("wb")] = wb;
    o[QStringLiteral("demosaic")] = demosaic;
    return o;
}

raw::RawDecodeOptions raw::RawDecodeOptions::fromJson(const QJsonObject &o)
{
    RawDecodeOptions v;
    v.autoBright = o.value(QStringLiteral("autoBright")).toBool(true);
    v.autoBrightThreshold =
        static_cast<float>(o.value(QStringLiteral("autoBrightThreshold")).toDouble(0.01));
    v.highlight = o.value(QStringLiteral("highlight")).toInt(0);
    v.wb = o.value(QStringLiteral("wb")).toInt(Camera);
    v.demosaic = o.value(QStringLiteral("demosaic")).toInt(3);
    return v;
}
