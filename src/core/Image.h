#pragma once

#include <QImage>
#include <QString>

// Forward declaration so this header does not pull in libvips (and, with it,
// glib's `signals` identifier, which collides with Qt's `signals` macro). Only
// vips-aware translation units include <vips/vips.h>.
struct _VipsImage;

// Image is a reference-counted RAII handle around a libvips image — the currency
// the edit graph passes between nodes. libvips images are lazy: holding one is
// cheap (it describes a computation, not a materialised buffer), which is what
// makes a non-destructive node pipeline practical.
//
// Copies share the underlying image (ref-counted); this is fine because nodes
// treat inputs as immutable and produce new images rather than mutating them.
class Image {
public:
    Image() = default;

    // Wraps `img`, taking an additional reference (the caller keeps theirs).
    explicit Image(_VipsImage *img);

    // Wraps `img`, adopting the caller's existing reference (no extra ref).
    // Use for images returned by libvips operations, which you already own.
    static Image adopt(_VipsImage *img);

    // Creates a black RGB image of the given size (useful for tests/placeholders).
    static Image black(int width, int height);

    // Loads and normalises an image to 8-bit sRGB with an alpha channel via
    // libvips. On failure returns a null Image and sets *error (if provided).
    static Image fromFile(const QString &path, QString *error = nullptr);

    // Decodes an encoded image (jpg/png/…) from an in-memory buffer, normalised
    // like fromFile. The result is materialised into its own memory, so it does
    // not reference `data` after returning (safe to free the buffer). Used to
    // load the source embedded in a .lumen project.
    static Image fromBytes(const void *data, qsizetype size, QString *error = nullptr);

    // Builds an Image from an interleaved 8-bit buffer (`bands` per pixel),
    // tagged sRGB so it round-trips through toQImage without colour mangling.
    // Copies the data, promoting to the float working format. Used by nodes that
    // edit pixels in a raw 8-bit buffer.
    static Image fromInterleaved(const void *data, int width, int height, int bands);

    // As above but from a float buffer (sRGB-encoded, 0..255, may exceed for
    // highlight headroom). The pipeline's native form — used by nodes that edit
    // pixels at full float precision.
    static Image fromInterleavedFloat(const float *data, int width, int height, int bands);

    Image(const Image &other);
    Image &operator=(const Image &other);
    Image(Image &&other) noexcept;
    Image &operator=(Image &&other) noexcept;
    ~Image();

    bool isNull() const { return m_image == nullptr; }
    int width() const;
    int height() const;

    // Materialises to a packed RGBA8 QImage for display / GPU upload.
    QImage toQImage() const;

    // Writes to `path` (format chosen from the extension) via libvips. The float
    // working image is converted to display-referred sRGB at `bits` (8 or 16) per
    // channel; a trailing alpha channel is dropped so formats like JPEG work.
    // Returns false and sets *error on failure.
    bool saveToFile(const QString &path, QString *error = nullptr) const
    {
        return saveToFile(path, -1, 8, error);
    }

    // As above, but `quality` (0-100) is applied to lossy formats (JPEG/WebP);
    // ignored for lossless formats. quality < 0 uses the encoder default.
    bool saveToFile(const QString &path, int quality, QString *error = nullptr) const
    {
        return saveToFile(path, quality, 8, error);
    }

    // As above, with `bits` (8 or 16) per channel. 16-bit only benefits lossless
    // formats (PNG/TIFF); JPEG/WebP are 8-bit regardless.
    bool saveToFile(const QString &path, int quality, int bits, QString *error = nullptr) const;

    // Raw handle for node implementations (only meaningful in vips-aware TUs).
    _VipsImage *handle() const { return m_image; }

private:
    _VipsImage *m_image = nullptr;
};
