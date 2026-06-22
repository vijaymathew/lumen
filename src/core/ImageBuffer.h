#pragma once

#include <QImage>
#include <QString>

// ImageBuffer owns a decoded image, loaded through libvips so the pipeline is
// routed through vips from day one (RAW/large-file handling lands later via the
// same path). For milestone 1 it exposes the result as a packed RGBA8 QImage
// ready to upload as a GPU texture.
//
// This is intentionally minimal: no edit graph yet, no tiling, no caching. It
// is the "load and display" half of milestone 1.
class ImageBuffer {
public:
    ImageBuffer() = default;

    // libvips lifecycle. Kept here (rather than in main.cpp) so that the only
    // translation unit including vips/glib headers is ImageBuffer.cpp — glib's
    // `gio` headers use `signals` as an identifier, which collides with Qt's
    // `signals` macro if a Qt header is parsed first in the same TU.
    static bool initLibrary(const char *argv0);
    static void shutdownLibrary();

    // Loads `path` via libvips, converting to 8-bit sRGB with an alpha channel.
    // On failure returns false and sets errorString().
    bool load(const QString &path);

    bool isNull() const { return m_image.isNull(); }
    const QImage &image() const { return m_image; }
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }

    const QString &errorString() const { return m_error; }

private:
    QImage m_image;
    QString m_error;
};
