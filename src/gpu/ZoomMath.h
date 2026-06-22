#pragma once

#include <QPointF>
#include <QSizeF>

#include <algorithm>

// Pure view-geometry math shared by the renderer (CanvasWidget::computeMvp) and
// the wheel handler (CanvasWidget::zoomAt), and unit-tested in isolation. All
// sizes/points are in device pixels.
//
// The image is displayed fit-to-widget, then scaled by `zoom` and offset by
// `pan`, centred in the widget.
namespace zoommath {

// Scale that fits the whole image inside the widget at zoom 1.
inline float fitScale(QSizeF widget, QSizeF image)
{
    return std::min(static_cast<float>(widget.width() / image.width()),
                    static_cast<float>(widget.height() / image.height()));
}

// Displayed size of the image (device pixels) at the given zoom.
inline QSizeF displayedSize(QSizeF widget, QSizeF image, float zoom)
{
    const float s = fitScale(widget, image) * zoom;
    return QSizeF(image.width() * s, image.height() * s);
}

// Top-left corner of the displayed image, in widget device pixels.
inline QPointF imageTopLeft(QSizeF widget, QSizeF image, float zoom, QPointF pan)
{
    const QSizeF d = displayedSize(widget, image, zoom);
    return QPointF((widget.width() - d.width()) * 0.5 + pan.x(),
                   (widget.height() - d.height()) * 0.5 + pan.y());
}

// Image pixel coordinate currently under the given screen (widget) point.
inline QPointF imagePixelAt(QSizeF widget, QSizeF image, float zoom, QPointF pan,
                            QPointF screen)
{
    const QPointF tl = imageTopLeft(widget, image, zoom, pan);
    const QSizeF d = displayedSize(widget, image, zoom);
    return QPointF((screen.x() - tl.x()) / d.width() * image.width(),
                   (screen.y() - tl.y()) / d.height() * image.height());
}

// New pan such that, after changing oldZoom -> newZoom, the same image point
// stays under `cursor` (cursor-centred zoom).
inline QPointF panForZoom(QSizeF widget, QSizeF image, float oldZoom, float newZoom,
                          QPointF oldPan, QPointF cursor)
{
    const QPointF tl0 = imageTopLeft(widget, image, oldZoom, oldPan);
    const QSizeF d0 = displayedSize(widget, image, oldZoom);
    // Normalised position of the cursor within the displayed image.
    const double u = (cursor.x() - tl0.x()) / d0.width();
    const double v = (cursor.y() - tl0.y()) / d0.height();

    const QSizeF d1 = displayedSize(widget, image, newZoom);
    return QPointF(cursor.x() - (widget.width() - d1.width()) * 0.5 - u * d1.width(),
                   cursor.y() - (widget.height() - d1.height()) * 0.5 - v * d1.height());
}

} // namespace zoommath
