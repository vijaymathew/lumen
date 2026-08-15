#pragma once

#include "core/Vignette.h" // VignetteParams
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// VignettePanel is the floating tool card for the creative (post-crop) vignette:
// an enable toggle plus Amount / Midpoint / Roundness / Feather sliders. Like
// GrainPanel it drives a live GPU op (the present pass), so valuesChanged() just
// refreshes the preview — no base re-bake. Closes on Esc/Enter.
class VignettePanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit VignettePanel(QWidget *parent = nullptr);

    void reveal(const VignetteParams &values);

signals:
    void valuesChanged(const VignetteParams &values);

private:
    void onChanged();
    void refreshLabels();
    VignetteParams currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_amount = nullptr;    // -100..100
    QSlider *m_midpoint = nullptr;  // 0..100
    QSlider *m_roundness = nullptr; // -100..100
    QSlider *m_feather = nullptr;   // 0..100
    QLabel *m_amountValue = nullptr;
    QLabel *m_midpointValue = nullptr;
    QLabel *m_roundnessValue = nullptr;
    QLabel *m_featherValue = nullptr;
};
