#pragma once

#include "core/DefringeNode.h" // DefringeNode::Values
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// DefringePanel is the floating tool card for chromatic-fringe suppression: an
// enable toggle plus Purple / Green amount sliders and an edge Threshold. Like
// DenoisePanel it drives a baked op — valuesChanged() triggers a debounced base
// re-bake in MainWindow. Closes on Esc/Enter.
class DefringePanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit DefringePanel(QWidget *parent = nullptr);

    void reveal(const DefringeNode::Values &values);

signals:
    void valuesChanged(const DefringeNode::Values &values);

private:
    void onChanged();
    void refreshLabels();
    DefringeNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_purple = nullptr;
    QSlider *m_green = nullptr;
    QSlider *m_threshold = nullptr;
    QLabel *m_purpleValue = nullptr;
    QLabel *m_greenValue = nullptr;
    QLabel *m_thresholdValue = nullptr;
};
