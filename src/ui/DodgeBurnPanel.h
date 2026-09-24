#pragma once

#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// DodgeBurnPanel is the floating tool card for the dodge & burn brushes: paint
// over the image to lighten (Dodge) or darken (Burn) it, Erase to take painting
// back, Clear to reset the active brush. Size/Hardness control the brush;
// Exposure (strength) and Range (which tones are affected) apply to the whole
// effect and stay adjustable after painting. The painting reuses the shared
// brush + session-undo infrastructure, like HealPanel.
class DodgeBurnPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit DodgeBurnPanel(QWidget *parent = nullptr);

    // range: 0 shadows, 1 midtones, 2 highlights (DodgeBurnNode::Range).
    void reveal(bool dodge, int size, int hardness, bool add, int exposure, int range);
    // Reflect externally-changed size/hardness (s/h + wheel) without re-emitting.
    void setBrushParams(int size, int hardness);

signals:
    void brushChanged(int size, int hardness, bool add);
    void modeChanged(bool dodge);
    void effectChanged(int exposure, int range);
    void clearRequested();

private:
    QSlider *addBrushRow(const QString &name, int min, int max, int def, QLabel **valueOut);
    void setDodge(bool dodge);
    void setAdd(bool add);
    void setRange(int range);
    void emitBrush();
    void emitEffect();

    QPushButton *m_dodgeButton = nullptr;
    QPushButton *m_burnButton = nullptr;
    QPushButton *m_paintButton = nullptr;
    QPushButton *m_eraseButton = nullptr;
    QPushButton *m_rangeButtons[3] = {};
    QSlider *m_size = nullptr;
    QSlider *m_hardness = nullptr;
    QSlider *m_exposure = nullptr;
    QLabel *m_sizeValue = nullptr;
    QLabel *m_hardnessValue = nullptr;
    QLabel *m_exposureValue = nullptr;
    bool m_dodge = true;
    bool m_add = true;
    int m_range = 1;
};
