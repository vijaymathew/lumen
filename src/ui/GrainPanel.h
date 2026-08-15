#pragma once

#include "core/GrainNode.h" // GrainNode::Values
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// GrainPanel is the floating tool card for film grain: an enable toggle, an
// Amount slider and a Grain-size slider. It mirrors MonoPanel — grain is a live
// GPU op, so valuesChanged() just drives the preview (no base re-bake). Closes
// on Esc/Enter.
class GrainPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit GrainPanel(QWidget *parent = nullptr);

    void reveal(const GrainNode::Values &values);

signals:
    void valuesChanged(const GrainNode::Values &values);

private:
    void onChanged();
    void refreshLabels();
    GrainNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_amount = nullptr;
    QSlider *m_size = nullptr; // stores size*10 (1.0..8.0 → 10..80)
    QLabel *m_amountValue = nullptr;
    QLabel *m_sizeValue = nullptr;
};
