#include "ui/MaskGizmo.h"

#include "gpu/CanvasWidget.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr double kHandleR = 7.0;   // hit radius (logical px)
constexpr double kHandleDraw = 5.0; // drawn radius
} // namespace

MaskGizmo::MaskGizmo(CanvasWidget *canvas, QWidget *parent)
    : QWidget(parent), m_canvas(canvas)
{
    setObjectName(QStringLiteral("maskGizmo"));
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
}

void MaskGizmo::setSpec(const MaskSpec &spec)
{
    m_spec = spec;
    const bool geometric =
        spec.type == MaskSpec::LinearGradient || spec.type == MaskSpec::Radial;
    setVisible(geometric);
    if (geometric)
        update();
}

QPointF MaskGizmo::toWidget(QPointF norm) const
{
    return m_canvas->widgetForNormalized(norm);
}

QPointF MaskGizmo::toNorm(QPointF widget) const
{
    return m_canvas->normalizedForWidget(widget);
}

MaskGizmo::Handle MaskGizmo::hitTest(const QPointF &p) const
{
    const auto near = [&](QPointF h) {
        return QLineF(p, h).length() <= kHandleR;
    };
    if (m_spec.type == MaskSpec::LinearGradient) {
        if (near(toWidget(m_spec.gradFrom)))
            return GradFrom;
        if (near(toWidget(m_spec.gradTo)))
            return GradTo;
        // On the line?
        const QPointF a = toWidget(m_spec.gradFrom), b = toWidget(m_spec.gradTo);
        const QLineF line(a, b);
        const double len = line.length();
        if (len > 1.0) {
            const double t = QPointF::dotProduct(p - a, b - a) / (len * len);
            if (t >= 0 && t <= 1) {
                const QPointF proj = a + t * (b - a);
                if (QLineF(p, proj).length() <= kHandleR)
                    return GradLine;
            }
        }
    } else if (m_spec.type == MaskSpec::Radial) {
        const QPointF c = toWidget(m_spec.center);
        if (near(c))
            return Center;
        if (near(toWidget({m_spec.center.x() + m_spec.radiusX, m_spec.center.y()})))
            return RadiusX;
        if (near(toWidget({m_spec.center.x(), m_spec.center.y() + m_spec.radiusY})))
            return RadiusY;
    }
    return None;
}

void MaskGizmo::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor line(255, 255, 255, 230), halo(0, 0, 0, 150), fill(255, 255, 255, 210);

    const auto handle = [&](QPointF w) {
        p.setPen(QPen(halo, 3.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(w, kHandleDraw, kHandleDraw);
        p.setPen(QPen(line, 1.4));
        p.setBrush(fill);
        p.drawEllipse(w, kHandleDraw, kHandleDraw);
    };

    if (m_spec.type == MaskSpec::LinearGradient) {
        const QPointF a = toWidget(m_spec.gradFrom), b = toWidget(m_spec.gradTo);
        p.setPen(QPen(halo, 3.0));
        p.drawLine(a, b);
        p.setPen(QPen(line, 1.4));
        p.drawLine(a, b);
        handle(a);
        handle(b);
    } else if (m_spec.type == MaskSpec::Radial) {
        // Sample the ellipse boundary in normalised space → widget (handles aspect).
        QPolygonF poly;
        for (int i = 0; i <= 48; ++i) {
            const double t = i * (2.0 * M_PI / 48.0);
            poly << toWidget({m_spec.center.x() + m_spec.radiusX * std::cos(t),
                              m_spec.center.y() + m_spec.radiusY * std::sin(t)});
        }
        p.setPen(QPen(halo, 3.0));
        p.drawPolyline(poly);
        p.setPen(QPen(line, 1.4));
        p.drawPolyline(poly);
        handle(toWidget(m_spec.center));
        handle(toWidget({m_spec.center.x() + m_spec.radiusX, m_spec.center.y()}));
        handle(toWidget({m_spec.center.x(), m_spec.center.y() + m_spec.radiusY}));
    }
}

void MaskGizmo::forwardToCanvas(QEvent *event)
{
    // The gizmo sits directly over the canvas at the same geometry, so local
    // coordinates map 1:1; forward the event so the canvas can pan/zoom.
    if (m_canvas)
        QCoreApplication::sendEvent(m_canvas, event);
}

void MaskGizmo::mousePressEvent(QMouseEvent *e)
{
    m_active = hitTest(e->position());
    if (m_active == None) {
        forwardToCanvas(e); // start a canvas pan instead
        return;
    }
    m_dragStartNorm = toNorm(e->position());
    if (m_active == GradLine)
        m_grabOffset = m_dragStartNorm - m_spec.gradFrom;
    else if (m_active == Center)
        m_grabOffset = m_dragStartNorm - m_spec.center;
}

void MaskGizmo::mouseMoveEvent(QMouseEvent *e)
{
    if (m_active == None) {
        forwardToCanvas(e); // canvas pan / hover
        return;
    }
    const QPointF n = toNorm(e->position());
    switch (m_active) {
    case GradFrom: m_spec.gradFrom = n; break;
    case GradTo: m_spec.gradTo = n; break;
    case GradLine: {
        const QPointF delta = (n - m_grabOffset) - m_spec.gradFrom;
        m_spec.gradFrom += delta;
        m_spec.gradTo += delta;
        break;
    }
    case Center: m_spec.center = n - m_grabOffset; break;
    case RadiusX: m_spec.radiusX = std::max(0.01, std::abs(n.x() - m_spec.center.x())); break;
    case RadiusY: m_spec.radiusY = std::max(0.01, std::abs(n.y() - m_spec.center.y())); break;
    default: break;
    }
    update();
    emit changed(m_spec);
}

void MaskGizmo::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_active != None) {
        m_active = None;
        emit editFinished(m_spec);
    } else {
        forwardToCanvas(e);
    }
}

void MaskGizmo::wheelEvent(QWheelEvent *e)
{
    forwardToCanvas(e); // the gizmo never uses the wheel; let the canvas zoom
}
