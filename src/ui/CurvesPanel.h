#pragma once

#include "core/Curve.h"

#include <QWidget>

#include <vector>

// CurvesPanel is the pointer-first tone-curve editor — a floating card with a
// square plot (DESIGN.md §4.4). Click empty space to add a point, drag to move
// (endpoints are x-locked), drag a point out of the plot or press Delete to
// remove it. Arrow keys nudge the selected point (keyboard as garnish). The
// title bar is the drag handle for repositioning the card.
class CurvesPanel : public QWidget {
    Q_OBJECT

public:
    explicit CurvesPanel(QWidget *parent = nullptr);

    void reveal(const Curve &curve);

signals:
    void curveChanged(const Curve &curve);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRect plotRect() const;
    QPointF toWidget(const QPointF &curvePoint) const; // curve [0,1] -> widget px
    QPointF toCurve(const QPointF &widgetPoint) const; // widget px -> curve [0,1]
    int pointAt(const QPointF &widgetPoint) const;      // index or -1
    bool isEndpoint(int index) const;
    void setSelectedPosition(double x, double y); // clamp + endpoint rules
    void emitCurve();

    std::vector<QPointF> m_points; // editable model, sorted by x
    int m_selected = -1;

    bool m_dragPoint = false;
    bool m_dragWindow = false;
    QPoint m_windowDragOffset;
};
