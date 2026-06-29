#pragma once

#include "core/GrainNode.h" // GrainNode::Values

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// GrainPanel is the floating tool card for film grain: an enable toggle, an
// Amount slider and a Grain-size slider. It mirrors MonoPanel — grain is a live
// GPU op, so valuesChanged() just drives the preview (no base re-bake). Closes
// on Esc/Enter.
class GrainPanel : public QWidget {
    Q_OBJECT

public:
    explicit GrainPanel(QWidget *parent = nullptr);

    void reveal(const GrainNode::Values &values);

signals:
    void valuesChanged(const GrainNode::Values &values);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(const QString &name, int min, int max, QLabel **valueOut);
    void onChanged();
    void refreshLabels();
    GrainNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_amount = nullptr;
    QSlider *m_size = nullptr; // stores size*10 (1.0..8.0 → 10..80)
    QLabel *m_amountValue = nullptr;
    QLabel *m_sizeValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
