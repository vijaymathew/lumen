// vips (and the glib headers it pulls in) MUST be included before any Qt header
// — glib uses `signals` as an identifier and Qt defines it as a macro. Image.h
// only references QString, but keep this ordering to be safe.
#include <vips/vips.h>

#include "core/Image.h"

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
