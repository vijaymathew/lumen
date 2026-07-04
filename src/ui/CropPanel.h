#pragma once

#include "core/CropState.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// CropPanel is the floating tool card for Crop & Rotate: aspect-ratio presets,
// 90° rotate (CW/CCW), horizontal/vertical flip, a fine straighten slider, and
// reset. It fires granular actions; MainWindow applies them to the graph's
// CropState. Closes on Esc/Enter.
class CropPanel : public QWidget {
    Q_OBJECT

public:
    explicit CropPanel(QWidget *parent = nullptr);

    // Seeds button states from the current crop; `originalAspect` is the source's
    // width/height (for the "Original" preset).
    void reveal(const CropState &crop, double originalAspect);

signals:
    void aspectChanged(double aspect); // width/height; 0 = free
    void rotateRequested(int deltaCW); // +90 (CW) or -90 (CCW)
    void flipRequested(bool horizontal);
    void straightenChanged(double degrees); // fine tilt, [-45, 45]
    void resetRequested();
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void selectAspectButton(int index);

    QVector<QPushButton *> m_aspectButtons;
    QPushButton *m_flipH = nullptr;
    QPushButton *m_flipV = nullptr;
    QSlider *m_straighten = nullptr;
    QLabel *m_straightenValue = nullptr;
    double m_originalAspect = 1.0;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
