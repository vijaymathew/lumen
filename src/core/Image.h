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

    // Writes to `path` (format chosen from the extension) via libvips. A
    // trailing alpha channel is dropped so formats like JPEG work. Returns false
    // and sets *error on failure.
    bool saveToFile(const QString &path, QString *error = nullptr) const;

    // Raw handle for node implementations (only meaningful in vips-aware TUs).
    _VipsImage *handle() const { return m_image; }

private:
    _VipsImage *m_image = nullptr;
};
