// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/Histogram.h"

#include "core/Image.h"

#include <algorithm>

HistogramData computeHistogram(const Image &image, int maxDim)
{
    HistogramData out;
    if (image.isNull())
        return out;

    VipsImage *src = image.handle();
    const int w = vips_image_get_width(src), h = vips_image_get_height(src);
    if (w <= 0 || h <= 0)
        return out;

    // Each operation owns `cur`; replace() swaps and unrefs the previous handle.
    VipsImage *cur = src;
    g_object_ref(cur);
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };
    const auto fail = [&]() {
        g_object_unref(cur);
        return out; // out still invalid
    };

    // 1. Downsample for speed (never upsample).
    const double scale = std::min(1.0, static_cast<double>(maxDim) / std::max(w, h));
    if (scale < 1.0) {
        VipsImage *small = nullptr;
        if (vips_resize(cur, &small, scale, nullptr))
            return fail();
        replace(small);
    }

    // 2. Keep the three colour bands (drop alpha / extras).
    if (vips_image_get_bands(cur) > 3) {
        VipsImage *rgb = nullptr;
        if (vips_extract_band(cur, &rgb, 0, "n", 3, nullptr))
            return fail();
        replace(rgb);
    }

    // 3. Display-referred 8-bit: vips_cast clips the float sRGB (0..255, possibly
    //    with highlight headroom) into the 0..255 uchar range.
    VipsImage *disp = nullptr;
    if (vips_cast(cur, &disp, VIPS_FORMAT_UCHAR, nullptr))
        return fail();
    replace(disp);

    // 4. 256-bin histogram (width=256, height=1, one band per colour channel).
    VipsImage *hist = nullptr;
    if (vips_hist_find(cur, &hist, nullptr))
        return fail();
    replace(hist);

    const int bands = std::min(vips_image_get_bands(cur), 3);
    void *buf = vips_image_write_to_memory(cur, nullptr);
    g_object_unref(cur);
    if (!buf)
        return out;

    const auto *counts = static_cast<const unsigned int *>(buf);
    for (int level = 0; level < 256; ++level)
        for (int c = 0; c < bands; ++c) {
            const uint32_t v = counts[level * bands + c];
            out.bins[c][level] = v;
            out.peak = std::max(out.peak, v);
        }
    g_free(buf);
    out.valid = true;
    return out;
}
