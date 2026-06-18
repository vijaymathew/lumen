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
    return rotation == 0 && !flipH && !flipV && rect.x() <= 1e-6 && rect.y() <= 1e-6
        && rect.width() >= 1.0 - 1e-6 && rect.height() >= 1.0 - 1e-6;
}

void CropState::sanitize()
{
    rotation = ((rotation % 360) + 360) % 360;
    rotation = (rotation / 90) * 90; // snap to a quarter turn
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

    // 3. Extract the crop rect from the oriented frame.
    const int ow = cur->Xsize, oh = cur->Ysize;
    int left = static_cast<int>(std::lround(crop.rect.x() * ow));
    int top = static_cast<int>(std::lround(crop.rect.y() * oh));
    int w = static_cast<int>(std::lround(crop.rect.width() * ow));
    int h = static_cast<int>(std::lround(crop.rect.height() * oh));
    left = std::clamp(left, 0, ow - 1);
    top = std::clamp(top, 0, oh - 1);
    w = std::clamp(w, 1, ow - left);
    h = std::clamp(h, 1, oh - top);
    if (left == 0 && top == 0 && w == ow && h == oh)
        return Image::adopt(cur); // rotation/flip only, no sub-rect

    VipsImage *o = nullptr;
    if (vips_extract_area(cur, &o, left, top, w, h, nullptr)) {
        g_object_unref(cur);
        return img;
    }
    replace(o);
    return Image::adopt(cur);
}
