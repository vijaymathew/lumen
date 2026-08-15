#pragma once

#include "core/StructureNode.h" // StructureNode::Values
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// StructurePanel is the floating tool card for local contrast ("Structure" /
// Clarity): an enable toggle, a bipolar Amount slider (negative softens) and a
// Radius slider. It mirrors SharpenPanel — drives the preview via valuesChanged()
// (the base re-bakes) and closes on Esc/Enter.
class StructurePanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit StructurePanel(QWidget *parent = nullptr);

    void reveal(const StructureNode::Values &values);

signals:
    void valuesChanged(const StructureNode::Values &values);

private:
    void onChanged();
    void refreshLabels();
    StructureNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_amount = nullptr; // -100..100
    QSlider *m_radius = nullptr; // gaussian sigma in px (2..50)
    QLabel *m_amountValue = nullptr;
    QLabel *m_radiusValue = nullptr;
};
