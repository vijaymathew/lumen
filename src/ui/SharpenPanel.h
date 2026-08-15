#pragma once

#include "core/SharpenNode.h" // SharpenNode::Values
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// SharpenPanel is the floating tool card for sharpening: an enable toggle, an
// Amount slider and a Radius slider. It mirrors MonoPanel — drives the preview
// via valuesChanged() (the base re-bakes) and closes on Esc/Enter.
class SharpenPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit SharpenPanel(QWidget *parent = nullptr);

    void reveal(const SharpenNode::Values &values);

signals:
    void valuesChanged(const SharpenNode::Values &values);

private:
    void onChanged();
    void refreshLabels();
    SharpenNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_amount = nullptr;
    QSlider *m_radius = nullptr; // stores radius*10 (0.3..4.0 → 3..40)
    QLabel *m_amountValue = nullptr;
    QLabel *m_radiusValue = nullptr;
};
