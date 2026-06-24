#pragma once

#include "core/MaskSpec.h"

#include <QWidget>

class CanvasWidget;

// MaskGizmo is an on-canvas overlay for editing a layer's geometric mask: a
// draggable line for a linear gradient, or a draggable ellipse (centre + radius
// handles) for a radial mask. It maps between image-normalised coordinates (the
// MaskSpec) and screen positions via the canvas. Active only for gradient/radial
// masks; otherwise hidden.
class MaskGizmo : public QWidget {
    Q_OBJECT

public:
    explicit MaskGizmo(CanvasWidget *canvas, QWidget *parent = nullptr);

    void setSpec(const MaskSpec &spec); // geometry to display/edit; hides if not geometric
    const MaskSpec &spec() const { return m_spec; }
    void refresh() { update(); } // re-read canvas transform (after zoom/pan)

signals:
    void changed(const MaskSpec &spec);    // live drag
    void editFinished(const MaskSpec &spec); // on mouse release (commit point)

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum Handle { None, GradFrom, GradTo, GradLine, Center, RadiusX, RadiusY };
    Handle hitTest(const QPointF &widgetPos) const;
    QPointF toWidget(QPointF norm) const;
    QPointF toNorm(QPointF widget) const;
    // The gizmo overlays the whole canvas; events not on a handle are forwarded
    // to the canvas underneath so panning/zooming still work while it is shown.
    void forwardToCanvas(QEvent *event);

    CanvasWidget *m_canvas = nullptr;
    MaskSpec m_spec;
    Handle m_active = None;
    QPointF m_dragStartNorm;
    QPointF m_grabOffset; // for line/center moves
};
