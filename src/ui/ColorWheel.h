#pragma once

#include <QImage>
#include <QWidget>

// ColorWheel is a compact, reusable hue/saturation puck used by the colour-grade
// panel (one per Lift/Gamma/Gain). The puck position is reported as (x,y) in the
// unit disc with +x right and +y up; the disc is painted to show the tint each
// position produces (red up, matching ColorGradeNode's wheel axes). Double-click
// recenters.
class ColorWheel : public QWidget {
    Q_OBJECT

public:
    explicit ColorWheel(QWidget *parent = nullptr);

    QSize sizeHint() const override { return {116, 116}; }

    float x() const { return m_x; }
    float y() const { return m_y; }
    void setValue(float x, float y); // updates the puck without emitting

signals:
    void changed(float x, float y);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    void rebuildDisc();
    void setFromPos(const QPointF &pos); // widget coords → (x,y), clamps, emits
    QPointF puckCenter() const;          // widget coords of the current puck
    int radiusPx() const;

    float m_x = 0.0f;
    float m_y = 0.0f;
    QImage m_disc; // cached painted wheel, rebuilt on resize
};
