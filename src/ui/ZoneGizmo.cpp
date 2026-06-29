#include "ui/ZoneGizmo.h"

#include "gpu/CanvasWidget.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kHandleR = 7.0;    // hit radius (logical px)
constexpr double kHandleDraw = 5.0; // drawn radius
constexpr float kRotatePad = 0.06f; // rotate-handle offset above the shape (norm)
constexpr float kMinHalf = 0.004f;  // discard a drawn box smaller than this
constexpr int kMaxPolyPoints = 64;  // decimate freehand paths to this

// Rotates a shape-local offset into the normalised frame (inverse of the
// rotation evaluateMask uses for zone coverage).
QPointF rotLocal(QPointF local, float ca, float sa)
{
    return {local.x() * ca - local.y() * sa, local.x() * sa + local.y() * ca};
}
} // namespace

ZoneGizmo::ZoneGizmo(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent), m_canvas(canvas)
{
    setObjectName(QStringLiteral("zoneGizmo"));
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
}

void ZoneGizmo::setShapes(const std::vector<MaskZoneShape> &shapes)
{
    m_shapes = shapes;
    if (m_selected >= static_cast<int>(m_shapes.size()))
        m_selected = -1;
    update();
}

std::vector<MaskZoneShape> ZoneGizmo::shapes() const { return m_shapes; }

void ZoneGizmo::setTool(Tool tool)
{
    m_tool = tool;
    if (tool != Select) // leaving Select drops any handle hover state
        m_active = {};
    update();
}

bool ZoneGizmo::deleteSelected()
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_shapes.size()))
        return false;
    m_shapes.erase(m_shapes.begin() + m_selected);
    m_selected = -1;
    update();
    emit changed(m_shapes);
    emit editFinished(m_shapes);
    return true;
}

QPointF ZoneGizmo::toWidget(QPointF norm) const { return m_canvas->widgetForNormalized(norm); }
QPointF ZoneGizmo::toNorm(QPointF widget) const { return m_canvas->normalizedForWidget(widget); }

QPointF ZoneGizmo::localToNorm(const MaskZoneShape &s, QPointF local) const
{
    const float ca = std::cos(s.angle), sa = std::sin(s.angle);
    return s.center + rotLocal(local, ca, sa);
}

QVector<QPointF> ZoneGizmo::handleNorms(const MaskZoneShape &s) const
{
    // [EdgeX, EdgeY, Corner, Rotate] in normalised space.
    const float hx = static_cast<float>(s.half.x()), hy = static_cast<float>(s.half.y());
    return {localToNorm(s, {hx, 0}), localToNorm(s, {0, hy}), localToNorm(s, {hx, hy}),
            localToNorm(s, {0, -(hy + kRotatePad)})};
}

ZoneGizmo::Hit ZoneGizmo::hitTest(const QPointF &p) const
{
    if (m_selected < 0)
        return {};
    const MaskZoneShape &s = m_shapes[m_selected];
    const auto near = [&](QPointF wn) { return QLineF(p, toWidget(wn)).length() <= kHandleR; };

    if (s.kind == MaskZoneShape::Polygon) {
        for (int i = 0; i < s.points.size(); ++i)
            if (near(s.points[i]))
                return {HVertex, i};
        return {};
    }
    const QVector<QPointF> h = handleNorms(s);
    if (near(h[3]))
        return {HRotate, -1};
    if (near(h[2]))
        return {HCorner, -1};
    if (near(h[0]))
        return {HEdgeX, -1};
    if (near(h[1]))
        return {HEdgeY, -1};
    return {};
}

int ZoneGizmo::shapeAt(const QPointF &widgetPos) const
{
    const QPointF n = toNorm(widgetPos);
    const float px = static_cast<float>(n.x()), py = static_cast<float>(n.y());
    for (int i = static_cast<int>(m_shapes.size()) - 1; i >= 0; --i) {
        const MaskZoneShape &s = m_shapes[i];
        if (s.kind == MaskZoneShape::Polygon) {
            const int cnt = s.points.size();
            bool inside = false;
            for (int a = 0, b = cnt - 1; a < cnt; b = a++) {
                const double xi = s.points[a].x(), yi = s.points[a].y();
                const double xj = s.points[b].x(), yj = s.points[b].y();
                if (((yi > py) != (yj > py)) &&
                    (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12) + xi))
                    inside = !inside;
            }
            if (inside)
                return i;
            continue;
        }
        const float ca = std::cos(s.angle), sa = std::sin(s.angle);
        const float ox = px - static_cast<float>(s.center.x());
        const float oy = py - static_cast<float>(s.center.y());
        const float lx = ox * ca + oy * sa, ly = -ox * sa + oy * ca;
        const float hx = std::max(static_cast<float>(s.half.x()), 1e-4f);
        const float hy = std::max(static_cast<float>(s.half.y()), 1e-4f);
        const bool inside = (s.kind == MaskZoneShape::Ellipse)
                                ? ((lx / hx) * (lx / hx) + (ly / hy) * (ly / hy) <= 1.0f)
                                : (std::abs(lx) <= hx && std::abs(ly) <= hy);
        if (inside)
            return i;
    }
    return -1;
}

void ZoneGizmo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor halo(0, 0, 0, 150);
    const QColor add(255, 255, 255, 230), sub(255, 90, 90, 235);
    const QColor fill(255, 255, 255, 210);

    const auto handle = [&](QPointF wn) {
        const QPointF w = toWidget(wn);
        p.setPen(QPen(halo, 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(w, kHandleDraw, kHandleDraw);
        p.setPen(QPen(QColor(255, 255, 255, 230), 1.4));
        p.setBrush(fill);
        p.drawEllipse(w, kHandleDraw, kHandleDraw);
    };

    for (int i = 0; i < static_cast<int>(m_shapes.size()); ++i) {
        const MaskZoneShape &s = m_shapes[i];
        const bool selected = (i == m_selected);
        const QColor c = s.subtract ? sub : add;

        QPolygonF poly;
        if (s.kind == MaskZoneShape::Polygon) {
            for (const QPointF &pt : s.points)
                poly << toWidget(pt);
        } else if (s.kind == MaskZoneShape::Ellipse) {
            const float hx = static_cast<float>(s.half.x()), hy = static_cast<float>(s.half.y());
            for (int k = 0; k <= 48; ++k) {
                const double t = k * (2.0 * M_PI / 48.0);
                poly << toWidget(localToNorm(s, {hx * std::cos(t), hy * std::sin(t)}));
            }
        } else { // Rect
            const float hx = static_cast<float>(s.half.x()), hy = static_cast<float>(s.half.y());
            poly << toWidget(localToNorm(s, {-hx, -hy})) << toWidget(localToNorm(s, {hx, -hy}))
                 << toWidget(localToNorm(s, {hx, hy})) << toWidget(localToNorm(s, {-hx, hy}));
        }

        QPen outline(c, selected ? 2.0 : 1.4);
        if (s.subtract)
            outline.setStyle(Qt::DashLine);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(halo, selected ? 3.5 : 3.0));
        p.drawPolygon(poly);
        p.setPen(outline);
        p.drawPolygon(poly);

        if (selected) {
            if (s.kind == MaskZoneShape::Polygon) {
                for (const QPointF &pt : s.points)
                    handle(pt);
            } else {
                const QVector<QPointF> h = handleNorms(s);
                // Stem to the rotation handle.
                p.setPen(QPen(halo, 3.0));
                p.drawLine(toWidget(s.center), toWidget(h[3]));
                p.setPen(QPen(QColor(255, 255, 255, 230), 1.4));
                p.drawLine(toWidget(s.center), toWidget(h[3]));
                handle(s.center);
                for (const QPointF &hn : h)
                    handle(hn);
            }
        }
    }
}

void ZoneGizmo::forwardToCanvas(QEvent *event)
{
    // Prefer the MaskGizmo (if shown) so a gradient/radial mask remains editable
    // beneath us; it forwards its own misses to the canvas.
    QWidget *target = (m_fallthrough && m_fallthrough->isVisible())
                          ? m_fallthrough
                          : static_cast<QWidget *>(m_canvas);
    if (target)
        QCoreApplication::sendEvent(target, event);
}

void ZoneGizmo::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        forwardToCanvas(e);
        return;
    }
    const QPointF n = toNorm(e->position());

    if (m_tool != Select) {
        // Start a new shape under the active draw tool.
        m_drawing = true;
        m_dragStartNorm = n;
        MaskZoneShape s;
        s.subtract = m_subtract;
        if (m_tool == DrawFreehand) {
            s.kind = MaskZoneShape::Polygon;
            s.points = {n};
        } else {
            s.kind = (m_tool == DrawRect) ? MaskZoneShape::Rect : MaskZoneShape::Ellipse;
            s.center = n;
            s.half = {0.0, 0.0};
        }
        m_shapes.push_back(s);
        m_selected = static_cast<int>(m_shapes.size()) - 1;
        update();
        return;
    }

    // Select mode: handles of the selected shape first.
    m_active = hitTest(e->position());
    if (m_active.kind != HNone) {
        m_dragStartNorm = n;
        return;
    }
    // Otherwise pick the topmost shape under the cursor and start moving it.
    const int idx = shapeAt(e->position());
    if (idx >= 0) {
        m_selected = idx;
        m_moving = true;
        m_dragStartNorm = n;
        m_moveStartCenter = m_shapes[idx].center;
        m_movePoints = m_shapes[idx].points;
        update();
        return;
    }
    // Empty space: deselect and let the canvas pan.
    if (m_selected != -1) {
        m_selected = -1;
        update();
    }
    forwardToCanvas(e);
}

void ZoneGizmo::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF n = toNorm(e->position());

    if (m_drawing) {
        MaskZoneShape &s = m_shapes.back();
        if (s.kind == MaskZoneShape::Polygon) {
            if (s.points.isEmpty() || QLineF(s.points.back(), n).length() > 0.004)
                s.points.append(n);
        } else {
            const QPointF c = (m_dragStartNorm + n) / 2.0;
            double hx = std::abs(n.x() - m_dragStartNorm.x()) / 2.0;
            double hy = std::abs(n.y() - m_dragStartNorm.y()) / 2.0;
            if (m_tool == DrawCircle)
                hx = hy = std::max(hx, hy);
            s.center = c;
            s.half = {hx, hy};
        }
        update();
        emit changed(m_shapes);
        return;
    }

    if (m_active.kind != HNone && m_selected >= 0) {
        MaskZoneShape &s = m_shapes[m_selected];
        if (m_active.kind == HVertex && m_active.vertex >= 0 &&
            m_active.vertex < s.points.size()) {
            s.points[m_active.vertex] = n;
        } else {
            const float ca = std::cos(s.angle), sa = std::sin(s.angle);
            const float ox = static_cast<float>(n.x() - s.center.x());
            const float oy = static_cast<float>(n.y() - s.center.y());
            const float lx = ox * ca + oy * sa, ly = -ox * sa + oy * ca;
            switch (m_active.kind) {
            case HEdgeX: s.half.setX(std::max(std::abs(lx), kMinHalf)); break;
            case HEdgeY: s.half.setY(std::max(std::abs(ly), kMinHalf)); break;
            case HCorner:
                s.half.setX(std::max(std::abs(lx), kMinHalf));
                s.half.setY(std::max(std::abs(ly), kMinHalf));
                break;
            case HRotate: s.angle = std::atan2(ox, -oy); break;
            default: break;
            }
        }
        update();
        emit changed(m_shapes);
        return;
    }

    if (m_moving && m_selected >= 0) {
        MaskZoneShape &s = m_shapes[m_selected];
        const QPointF delta = n - m_dragStartNorm;
        if (s.kind == MaskZoneShape::Polygon) {
            for (int i = 0; i < s.points.size() && i < m_movePoints.size(); ++i)
                s.points[i] = m_movePoints[i] + delta;
        } else {
            s.center = m_moveStartCenter + delta;
        }
        update();
        emit changed(m_shapes);
        return;
    }

    forwardToCanvas(e); // hover / canvas pan
}

void ZoneGizmo::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drawing) {
        m_drawing = false;
        MaskZoneShape &s = m_shapes.back();
        bool discard = false;
        if (s.kind == MaskZoneShape::Polygon) {
            // Decimate the freehand path to a bounded vertex count.
            if (s.points.size() < 3) {
                discard = true;
            } else if (s.points.size() > kMaxPolyPoints) {
                const int step = (s.points.size() + kMaxPolyPoints - 1) / kMaxPolyPoints;
                QVector<QPointF> dec;
                for (int i = 0; i < s.points.size(); i += step)
                    dec.append(s.points[i]);
                s.points = dec;
            }
        } else if (s.half.x() < kMinHalf && s.half.y() < kMinHalf) {
            discard = true; // a click, not a drag
        }
        if (discard) {
            m_shapes.pop_back();
            m_selected = -1;
            update();
            emit changed(m_shapes);
            return;
        }
        update();
        emit editFinished(m_shapes);
        return;
    }

    if (m_active.kind != HNone || m_moving) {
        m_active = {};
        m_moving = false;
        emit editFinished(m_shapes);
        return;
    }

    forwardToCanvas(e);
}

void ZoneGizmo::wheelEvent(QWheelEvent *e)
{
    forwardToCanvas(e); // never used here; let the canvas zoom
}
