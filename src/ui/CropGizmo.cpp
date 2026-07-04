#include "ui/CropGizmo.h"

#include "gpu/CanvasWidget.h"

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kHandleR = 9.0;  // hit radius (logical px)
constexpr double kMinSize = 0.03; // smallest crop edge (normalized)
constexpr double kEdgeGuardPx = 2.0; // match core/CropState straightenSafeRect

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

QRectF lerpRect(const QRectF &a, const QRectF &b, double f)
{
    return QRectF(QPointF(a.left() + (b.left() - a.left()) * f,
                          a.top() + (b.top() - a.top()) * f),
                  QPointF(a.right() + (b.right() - a.right()) * f,
                          a.bottom() + (b.bottom() - a.bottom()) * f));
}
} // namespace

CropGizmo::CropGizmo(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent), m_canvas(canvas)
{
    setObjectName(QStringLiteral("cropGizmo"));
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
}

void CropGizmo::setRect(const QRectF &rect)
{
    m_rect = rect;
    update();
}

void CropGizmo::setAspect(double aspect)
{
    m_aspect = aspect;
    if (m_aspect > 0.0) {
        // Re-fit the current rect to the new ratio (width/height in display px),
        // centred, clamped into [0,1].
        const QSizeF frame = m_canvas->effectiveImageSize();
        if (frame.width() > 0 && frame.height() > 0) {
            const double normAspect = m_aspect * frame.height() / frame.width(); // w/h
            const QPointF c = m_rect.center();
            double w = m_rect.width();
            double h = w / normAspect;
            if (h > 1.0) { h = 1.0; w = h * normAspect; }
            if (w > 1.0) { w = 1.0; h = w / normAspect; }
            double x = clamp01(c.x() - w / 2);
            double y = clamp01(c.y() - h / 2);
            if (x + w > 1.0) x = 1.0 - w;
            if (y + h > 1.0) y = 1.0 - h;
            m_rect = QRectF(x, y, w, h);
            emit changed(m_rect);
            emit editFinished(m_rect);
        }
    }
    update();
}

void CropGizmo::setStraighten(double degrees)
{
    m_straighten = degrees;
    update();
}

QPointF CropGizmo::sourceForOriented(QPointF n) const
{
    // Same construction as CanvasWidget::cropTexXform's straightenInv (verified to
    // match the export path): a pixel-space rotation by -straighten about centre.
    const QSizeF f = m_canvas->effectiveImageSize();
    const float ow = static_cast<float>(f.width()), oh = static_cast<float>(f.height());
    if (ow <= 0.0f || oh <= 0.0f)
        return n;
    QMatrix4x4 m;
    m.translate(0.5f, 0.5f);
    m.scale(1.0f / ow, 1.0f / oh);
    m.rotate(static_cast<float>(-m_straighten), 0.0f, 0.0f, 1.0f);
    m.scale(ow, oh);
    m.translate(-0.5f, -0.5f);
    return m.map(n);
}

bool CropGizmo::rectSafe(const QRectF &r) const
{
    if (std::abs(m_straighten) < 1e-6)
        return true; // no tilt → the whole [0,1] frame is valid
    const QSizeF f = m_canvas->effectiveImageSize();
    const double ow = f.width(), oh = f.height();
    if (ow <= 0.0 || oh <= 0.0)
        return true;
    // The tilt's valid region is convex, so testing the four corners suffices.
    // A corner is valid when its pre-straighten point lands inside the frame,
    // minus the anti-alias guard. A small epsilon absorbs the rounding at the
    // auto-inset boundary so the initial rect isn't spuriously rejected.
    const double gx = kEdgeGuardPx / ow, gy = kEdgeGuardPx / oh, eps = 1e-3;
    const QPointF corners[4] = {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()};
    for (const QPointF &cpt : corners) {
        const QPointF s = sourceForOriented(cpt);
        if (s.x() < gx - eps || s.x() > 1.0 - gx + eps || s.y() < gy - eps
            || s.y() > 1.0 - gy + eps)
            return false;
    }
    return true;
}

QPointF CropGizmo::toWidget(QPointF norm) const { return m_canvas->widgetForNormalized(norm); }
QPointF CropGizmo::toNorm(QPointF widget) const { return m_canvas->normalizedForWidget(widget); }

CropGizmo::Handle CropGizmo::hitTest(const QPointF &p) const
{
    const auto near = [&](QPointF n) { return QLineF(p, toWidget(n)).length() <= kHandleR; };
    const double l = m_rect.left(), t = m_rect.top(), r = m_rect.right(), b = m_rect.bottom();
    const double cx = m_rect.center().x(), cy = m_rect.center().y();
    if (near({l, t})) return TL;
    if (near({r, t})) return TR;
    if (near({l, b})) return BL;
    if (near({r, b})) return BR;
    if (near({cx, t})) return T;
    if (near({cx, b})) return B;
    if (near({l, cy})) return L;
    if (near({r, cy})) return R;
    // Interior → move.
    const QPointF n = toNorm(p);
    if (m_rect.contains(n))
        return Move;
    return None;
}

void CropGizmo::applyDrag(const QPointF &nIn)
{
    const QPointF n(clamp01(nIn.x()), clamp01(nIn.y()));

    QRectF cand;
    if (m_active == Move) {
        QPointF tl = n - m_grabOffset;
        double x = std::clamp(tl.x(), 0.0, 1.0 - m_dragStartRect.width());
        double y = std::clamp(tl.y(), 0.0, 1.0 - m_dragStartRect.height());
        cand = QRectF(x, y, m_dragStartRect.width(), m_dragStartRect.height());
    } else {
        cand = computeResizeDrag(n);
    }

    // Keep the crop clear of a straighten tilt's transparent corners. The safe
    // set is convex in (l,t,r,b) and the drag started from a safe rect, so a
    // binary search along start→candidate finds the furthest valid rect (this
    // preserves the aspect ratio, since lerping two equal-aspect rects keeps it).
    if (rectSafe(cand)) {
        m_rect = cand;
        return;
    }
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = 0.5 * (lo + hi);
        (rectSafe(lerpRect(m_dragStartRect, cand, mid)) ? lo : hi) = mid;
    }
    m_rect = lerpRect(m_dragStartRect, cand, lo);
}

QRectF CropGizmo::computeResizeDrag(const QPointF &n) const
{
    double l = m_rect.left(), t = m_rect.top(), r = m_rect.right(), b = m_rect.bottom();

    switch (m_active) {
    case TL: l = std::min(n.x(), r - kMinSize); t = std::min(n.y(), b - kMinSize); break;
    case TR: r = std::max(n.x(), l + kMinSize); t = std::min(n.y(), b - kMinSize); break;
    case BL: l = std::min(n.x(), r - kMinSize); b = std::max(n.y(), t + kMinSize); break;
    case BR: r = std::max(n.x(), l + kMinSize); b = std::max(n.y(), t + kMinSize); break;
    case T:  t = std::min(n.y(), b - kMinSize); break;
    case B:  b = std::max(n.y(), t + kMinSize); break;
    case L:  l = std::min(n.x(), r - kMinSize); break;
    case R:  r = std::max(n.x(), l + kMinSize); break;
    default: break;
    }

    // Aspect constraint (width/height in display pixels). Convert to a normalized
    // ratio using the oriented frame's pixel size, then anchor at the corner/edge
    // opposite the one being dragged.
    if (m_aspect > 0.0) {
        const QSizeF frame = m_canvas->effectiveImageSize();
        if (frame.width() > 0 && frame.height() > 0) {
            const double normAspect = m_aspect * frame.height() / frame.width(); // w/h
            double w = r - l, h = b - t;
            const bool horizDriven = (m_active == L || m_active == R);
            if (horizDriven)
                h = w / normAspect; // width drives height
            else
                w = h * normAspect; // height (or corner) drives width
            // Anchor: keep the fixed side put.
            switch (m_active) {
            case TL: l = r - w; t = b - h; break;
            case TR: r = l + w; t = b - h; break;
            case BL: l = r - w; b = t + h; break;
            case BR: r = l + w; b = t + h; break;
            case T:  t = b - h; { double cx = (l + r) / 2; l = cx - w / 2; r = cx + w / 2; } break;
            case B:  b = t + h; { double cx = (l + r) / 2; l = cx - w / 2; r = cx + w / 2; } break;
            case L:  l = r - w; { double cy = (t + b) / 2; t = cy - h / 2; b = cy + h / 2; } break;
            case R:  r = l + w; { double cy = (t + b) / 2; t = cy - h / 2; b = cy + h / 2; } break;
            default: break;
            }
        }
    }

    l = clamp01(l); t = clamp01(t); r = clamp01(r); b = clamp01(b);
    if (r - l < kMinSize) r = std::min(1.0, l + kMinSize);
    if (b - t < kMinSize) b = std::min(1.0, t + kMinSize);
    return QRectF(QPointF(l, t), QPointF(r, b));
}

void CropGizmo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tl = toWidget(m_rect.topLeft());
    const QPointF br = toWidget(m_rect.bottomRight());
    const QRectF box(tl, br);

    // Dim outside the crop.
    QPainterPath outer;
    outer.addRect(QRectF(rect()));
    QPainterPath inner;
    inner.addRect(box);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(10, 10, 11, 150));
    p.drawPath(outer.subtracted(inner));

    // Rule-of-thirds grid + border.
    p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
    for (int i = 1; i < 3; ++i) {
        const double x = box.left() + box.width() * i / 3.0;
        const double y = box.top() + box.height() * i / 3.0;
        p.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
        p.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
    }
    p.setPen(QPen(QColor(0, 0, 0, 150), 3.0));
    p.drawRect(box);
    p.setPen(QPen(QColor(255, 255, 255, 235), 1.4));
    p.drawRect(box);

    // Resize handles: 4 corners + 4 edge midpoints.
    const QPointF mid(box.center());
    const QPointF handles[8] = {
        box.topLeft(),    box.topRight(),    box.bottomLeft(),  box.bottomRight(),
        {mid.x(), box.top()}, {mid.x(), box.bottom()},          // top, bottom
        {box.left(), mid.y()}, {box.right(), mid.y()},          // left, right
    };
    for (const QPointF &c : handles) {
        p.setPen(QPen(QColor(0, 0, 0, 150), 3.0));
        p.setBrush(QColor(255, 255, 255, 235));
        p.drawRect(QRectF(c.x() - 3, c.y() - 3, 6, 6));
    }
}

void CropGizmo::forwardToCanvas(QEvent *event)
{
    if (m_canvas)
        QCoreApplication::sendEvent(m_canvas, event);
}

void CropGizmo::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        forwardToCanvas(e);
        return;
    }
    m_active = hitTest(e->position());
    if (m_active == None) {
        forwardToCanvas(e); // pan
        return;
    }
    m_dragStartRect = m_rect;
    m_grabOffset = toNorm(e->position()) - m_rect.topLeft();
}

void CropGizmo::mouseMoveEvent(QMouseEvent *e)
{
    if (m_active == None) {
        forwardToCanvas(e);
        return;
    }
    applyDrag(toNorm(e->position()));
    update();
    emit changed(m_rect);
}

void CropGizmo::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_active != None) {
        m_active = None;
        emit editFinished(m_rect);
    } else {
        forwardToCanvas(e);
    }
}

void CropGizmo::wheelEvent(QWheelEvent *e)
{
    forwardToCanvas(e); // let the canvas zoom
}
