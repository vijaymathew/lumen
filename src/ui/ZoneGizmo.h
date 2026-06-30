#pragma once

#include "core/MaskSpec.h"

#include <QVector>
#include <QWidget>

#include <vector>

class CanvasWidget;

// ZoneGizmo is an on-canvas overlay for editing a mask's exclusive zone — the
// set of shapes (rectangle / oval / circle / freehand) that gate where the mask
// may act. It maps image-normalised coordinates (MaskZoneShape) to screen via
// the canvas, draws every shape, and (in Select mode) edits the selected one
// with move/resize/rotate handles or per-vertex drags for polygons. A draw tool
// creates new shapes by dragging. Non-handle clicks are forwarded to the canvas
// so panning/zooming still work, mirroring MaskGizmo.
class ZoneGizmo : public QWidget {
    Q_OBJECT

public:
    enum Tool { Select, DrawRect, DrawOval, DrawCircle, DrawFreehand };

    explicit ZoneGizmo(CanvasWidget *canvas, QWidget *parent = nullptr);

    void setShapes(const std::vector<MaskZoneShape> &shapes); // load from the layer
    std::vector<MaskZoneShape> shapes() const;

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setSubtract(bool subtract) { m_subtract = subtract; }
    // Where to pass clicks the zone editor doesn't consume: the MaskGizmo (so a
    // gradient/radial mask stays editable beneath the zone overlay) if visible,
    // else the canvas. MaskGizmo in turn forwards its own misses to the canvas.
    void setFallthrough(QWidget *w) { m_fallthrough = w; }

    bool hasSelection() const { return m_selected >= 0; }
    bool deleteSelected(); // remove the selected shape; emits edits; true if removed

    void refresh() { update(); } // re-read canvas transform (after zoom/pan)

signals:
    void changed(const std::vector<MaskZoneShape> &shapes);      // live drag
    void editFinished(const std::vector<MaskZoneShape> &shapes); // on commit
    // Emitted when a finished draw auto-returns the tool to Select, so the host
    // can re-check the panel's Select button.
    void toolReset();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Handle kinds on the selected shape. EdgeX/EdgeY/Corner resize symmetrically
    // about the centre; Vertex carries an index into a polygon's points.
    enum HandleKind { HNone, HMove, HRotate, HEdgeX, HEdgeY, HCorner, HVertex };
    struct Hit {
        HandleKind kind = HNone;
        int vertex = -1;
    };

    QPointF toWidget(QPointF norm) const;
    QPointF toNorm(QPointF widget) const;
    // Maps a shape-local point (lx,ly) to a normalised point using the shape's
    // centre/rotation (same frame as evaluateMask's zone coverage).
    QPointF localToNorm(const MaskZoneShape &s, QPointF local) const;
    QVector<QPointF> handleNorms(const MaskZoneShape &s) const; // resize/rotate handles
    Hit hitTest(const QPointF &widgetPos) const;          // tests the selected shape
    int shapeAt(const QPointF &widgetPos) const;          // topmost shape under the cursor
    void forwardToCanvas(QEvent *event);

    CanvasWidget *m_canvas = nullptr;
    QWidget *m_fallthrough = nullptr;
    std::vector<MaskZoneShape> m_shapes;
    Tool m_tool = Select;
    bool m_subtract = false;

    int m_selected = -1;
    Hit m_active;             // handle being dragged in Select mode
    bool m_drawing = false;   // a draw-tool shape is in progress
    bool m_moving = false;    // dragging a whole shape (Select mode)
    QPointF m_dragStartNorm;  // press point (normalised)
    QPointF m_moveStartCenter; // shape centre at move start
    QVector<QPointF> m_movePoints; // polygon points at move start
};
