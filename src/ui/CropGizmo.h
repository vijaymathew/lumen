#pragma once

#include <QRectF>
#include <QWidget>

class CanvasWidget;

// CropGizmo is the on-canvas crop-rectangle editor, shown over the oriented full
// image while the Crop tool is open. It maps the rect (normalized in the oriented
// frame) to/from the canvas via the canvas's effective-frame coordinate mapping,
// draws 8 resize handles + a rule-of-thirds grid, dims outside the rect, and
// optionally constrains to an aspect ratio. Non-handle clicks are forwarded to
// the canvas so panning/zoom still work (mirrors MaskGizmo/ZoneGizmo).
class CropGizmo : public QWidget {
    Q_OBJECT

public:
    explicit CropGizmo(CanvasWidget *canvas, QWidget *parent = nullptr);

    void setRect(const QRectF &rect); // normalized [0,1] in the oriented frame
    const QRectF &rect() const { return m_rect; }
    // Aspect ratio (width/height) to constrain to; <= 0 means free.
    void setAspect(double aspect);
    // Fine straighten angle (deg). When non-zero, drags are clamped so the crop
    // stays clear of the tilt's transparent corners.
    void setStraighten(double degrees);
    void refresh() { update(); } // re-read canvas transform (after zoom/pan)

signals:
    void changed(const QRectF &rect);      // live drag
    void editFinished(const QRectF &rect); // on release (commit)

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Handles: 4 corners, 4 edges, plus Move (interior).
    enum Handle { None, TL, TR, BL, BR, T, B, L, R, Move };
    Handle hitTest(const QPointF &widgetPos) const;
    QPointF toWidget(QPointF norm) const;
    QPointF toNorm(QPointF widget) const;
    void applyDrag(const QPointF &norm);            // updates m_rect for m_active handle
    QRectF computeResizeDrag(const QPointF &n) const; // candidate rect for a resize handle
    // Maps an oriented-frame point to its pre-straighten (source) frame point,
    // mirroring CanvasWidget::cropTexXform's straighten factor.
    QPointF sourceForOriented(QPointF orientedNorm) const;
    bool rectSafe(const QRectF &rect) const; // all corners inside the tilted frame
    void forwardToCanvas(QEvent *event);

    CanvasWidget *m_canvas = nullptr;
    QRectF m_rect{0.0, 0.0, 1.0, 1.0};
    double m_aspect = 0.0;     // <=0 free
    double m_straighten = 0.0; // fine tilt (deg)
    Handle m_active = None;
    QPointF m_grabOffset; // for Move
    QRectF m_dragStartRect;
};
