// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/CropState.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kMinFrac = 0.01; // smallest crop edge as a fraction of the frame
} // namespace

bool CropState::isIdentity() const
{
    return rotation == 0 && !flipH && !flipV && std::abs(straighten) <= 1e-6
        && rect.x() <= 1e-6 && rect.y() <= 1e-6 && rect.width() >= 1.0 - 1e-6
        && rect.height() >= 1.0 - 1e-6;
}

void CropState::sanitize()
{
    rotation = ((rotation % 360) + 360) % 360;
    rotation = (rotation / 90) * 90; // snap to a quarter turn
    straighten = std::clamp(straighten, -45.0, 45.0);
    double x = std::clamp(rect.x(), 0.0, 1.0 - kMinFrac);
    double y = std::clamp(rect.y(), 0.0, 1.0 - kMinFrac);
    double w = std::clamp(rect.width(), kMinFrac, 1.0 - x);
    double h = std::clamp(rect.height(), kMinFrac, 1.0 - y);
    rect = QRectF(x, y, w, h);
}

QJsonObject CropState::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("rotation")] = rotation;
    o[QStringLiteral("flipH")] = flipH;
    o[QStringLiteral("flipV")] = flipV;
    if (straighten != 0.0)
        o[QStringLiteral("straighten")] = straighten; // omit the common (0) case
    o[QStringLiteral("x")] = rect.x();
    o[QStringLiteral("y")] = rect.y();
    o[QStringLiteral("w")] = rect.width();
    o[QStringLiteral("h")] = rect.height();
    if (!enabled)
        o[QStringLiteral("enabled")] = false; // omit the common (true) case
    return o;
}

CropState CropState::fromJson(const QJsonObject &o)
{
    CropState c;
    c.rotation = o.value(QStringLiteral("rotation")).toInt(0);
    c.flipH = o.value(QStringLiteral("flipH")).toBool(false);
    c.flipV = o.value(QStringLiteral("flipV")).toBool(false);
    c.straighten = o.value(QStringLiteral("straighten")).toDouble(0.0); // back-compat: 0
    c.rect = QRectF(o.value(QStringLiteral("x")).toDouble(0.0),
                    o.value(QStringLiteral("y")).toDouble(0.0),
                    o.value(QStringLiteral("w")).toDouble(1.0),
                    o.value(QStringLiteral("h")).toDouble(1.0));
    c.enabled = o.value(QStringLiteral("enabled")).toBool(true); // back-compat: true
    c.sanitize();
    return c;
}

Image applyCrop(const Image &img, const CropState &cropIn)
{
    if (img.isNull() || !cropIn.enabled || cropIn.isIdentity())
        return img;
    CropState crop = cropIn;
    crop.sanitize();

    VipsImage *cur = img.handle();
    g_object_ref(cur);
    auto replace = [&cur](VipsImage *next) {
        g_object_unref(cur);
        cur = next;
    };

    // 1. Flip (before rotation, so rect is in the final oriented frame).
    if (crop.flipH) {
        VipsImage *o = nullptr;
        if (vips_flip(cur, &o, VIPS_DIRECTION_HORIZONTAL, nullptr)) {
            g_object_unref(cur);
            return img;
        }
        replace(o);
    }
    if (crop.flipV) {
        VipsImage *o = nullptr;
        if (vips_flip(cur, &o, VIPS_DIRECTION_VERTICAL, nullptr)) {
            g_object_unref(cur);
            return img;
        }
        replace(o);
    }

    // 2. Rotation in 90° steps.
    if (crop.rotation != 0) {
        const VipsAngle angle = crop.rotation == 90    ? VIPS_ANGLE_D90
                                : crop.rotation == 180  ? VIPS_ANGLE_D180
                                                        : VIPS_ANGLE_D270;
        VipsImage *o = nullptr;
        if (vips_rot(cur, &o, angle, nullptr)) {
            g_object_unref(cur);
            return img;
        }
        replace(o);
    }

    // Oriented frame size (pre-straighten). The crop rect is normalized against
    // this frame; straighten only tilts content beneath the axis-aligned rect.
    const int ow = cur->Xsize, oh = cur->Ysize;

    // 2.5 Fine straighten: rotate content about the frame centre. vips_rotate
    //     grows the canvas to the rotated bounding box, filling the new corners
    //     with the transparent background, so the oriented frame sits centred in
    //     a larger canvas. We offset the extract rect by that growth below.
    int offX = 0, offY = 0;
    if (std::abs(crop.straighten) > 1e-6) {
        VipsImage *o = nullptr;
        if (vips_rotate(cur, &o, crop.straighten, nullptr)) {
            g_object_unref(cur);
            return img;
        }
        replace(o);
        offX = (cur->Xsize - ow) / 2;
        offY = (cur->Ysize - oh) / 2;
    }
    const int cw = cur->Xsize, ch = cur->Ysize; // current (possibly enlarged) canvas

    // 3. Extract the crop rect from the oriented frame (offset into the enlarged
    //    straightened canvas).
    int left = offX + static_cast<int>(std::lround(crop.rect.x() * ow));
    int top = offY + static_cast<int>(std::lround(crop.rect.y() * oh));
    int w = static_cast<int>(std::lround(crop.rect.width() * ow));
    int h = static_cast<int>(std::lround(crop.rect.height() * oh));
    left = std::clamp(left, 0, cw - 1);
    top = std::clamp(top, 0, ch - 1);
    w = std::clamp(w, 1, cw - left);
    h = std::clamp(h, 1, ch - top);
    if (left == 0 && top == 0 && w == cw && h == ch)
        return Image::adopt(cur); // rotation/flip/straighten only, no sub-rect

    VipsImage *o = nullptr;
    if (vips_extract_area(cur, &o, left, top, w, h, nullptr)) {
        g_object_unref(cur);
        return img;
    }
    replace(o);
    return Image::adopt(cur);
}
