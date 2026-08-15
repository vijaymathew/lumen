#pragma once

#include "core/DenoiseNode.h" // DenoiseNode::Values
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// DenoisePanel is the floating tool card for noise reduction: an enable toggle,
// a Luma slider and a Chroma slider. It mirrors SharpenPanel — drives the
// preview via valuesChanged() (the base re-bakes) and closes on Esc/Enter.
class DenoisePanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit DenoisePanel(QWidget *parent = nullptr);

    void reveal(const DenoiseNode::Values &values);

signals:
    void valuesChanged(const DenoiseNode::Values &values);

private:
    void onChanged();
    void refreshLabels();
    DenoiseNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_luma = nullptr;
    QSlider *m_chroma = nullptr;
    QLabel *m_lumaValue = nullptr;
    QLabel *m_chromaValue = nullptr;
};
