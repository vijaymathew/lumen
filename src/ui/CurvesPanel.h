#pragma once

#include "core/CurvesNode.h"

#include <QWidget>

#include <array>
#include <vector>

// CurvesPanel is the pointer-first tone-curve editor — a floating card with a
// channel selector (RGB / R / G / B) and a square plot (DESIGN.md §4.4). Click
// empty space to add a point, drag to move (endpoints are x-locked), drag a
// point out of the plot or press Delete to remove it. Arrow keys nudge the
// selected point. The title bar drags the card.
class CurvesPanel : public QWidget {
    Q_OBJECT

public:
    explicit CurvesPanel(QWidget *parent = nullptr);

    void reveal(const ChannelCurves &curves);

signals:
    void curveChanged(const ChannelCurves &curves);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRect plotRect() const;
    QRect tabRect(int channel) const;
    QColor channelColor() const;
    QPointF toWidget(const QPointF &curvePoint) const;
    QPointF toCurve(const QPointF &widgetPoint) const;
    int pointAt(const QPointF &widgetPoint) const;
    bool isEndpoint(int index) const;
    void setSelectedPosition(double x, double y);
    void emitCurves();

    std::array<std::vector<QPointF>, 4> m_points; // master, R, G, B
    int m_channel = 0;
    int m_selected = -1;

    bool m_dragPoint = false;
    bool m_dragWindow = false;
    QPoint m_windowDragOffset;
};
