// vips (and the glib/gio headers it pulls in) MUST be included before any Qt
// header: glib uses `signals` as an identifier and Qt defines it as a macro, so
// parsing glib first avoids the collision. Do not reorder these includes.
#include <vips/vips.h>

#include "core/Image.h"
#include "core/ImageBuffer.h"

bool ImageBuffer::initLibrary(const char *argv0)
{
    // VIPS_INIT returns non-zero on failure.
    if (VIPS_INIT(argv0) != 0)
        return false;

    // vips defaults to using every core for each operation. That makes background
    // work — the live histogram recompute, heal/denoise bakes, export — grab all
    // cores in a burst and starve the UI thread, which shows up as sliders
    // dragging jankily while the histogram is open. Leave one core for the UI so
    // dragging stays smooth; background ops are marginally slower but non-blocking.
    const int cores = vips_concurrency_get();
    if (cores > 1)
        vips_concurrency_set(cores - 1);
    return true;
}

void ImageBuffer::shutdownLibrary()
{
    vips_shutdown();
}

bool ImageBuffer::load(const QString &path)
{
    m_error.clear();
    Image img = Image::fromFile(path, &m_error);
    if (img.isNull())
        return false;

    m_image = img.toQImage();
    if (m_image.isNull()) {
        m_error = QStringLiteral("Failed to decode pixels");
        return false;
    }
    return true;
}
