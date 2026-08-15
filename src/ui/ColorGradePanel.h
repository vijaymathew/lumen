#pragma once

#include "core/ColorGradeNode.h"
#include "ui/FloatingToolPanel.h"

class QCheckBox;
class QHBoxLayout;
class QSlider;
class ColorWheel;

// ColorGradePanel is the floating, draggable tool card for creative colour
// grading: three colour wheels (Lift / Gamma / Gain) each with a master luma
// slider, plus an enable toggle. It drives the live preview via valuesChanged()
// and closes on Esc/Enter. Tool panels float (DESIGN.md §4.6) — draggable
// anywhere but the wheels/sliders.
class ColorGradePanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit ColorGradePanel(QWidget *parent = nullptr);

    void reveal(const ColorGradeValues &values); // show, seeded with the values

signals:
    void valuesChanged(const ColorGradeValues &values);

private:
    struct Wheel {
        ColorWheel *wheel = nullptr;
        QSlider *master = nullptr;
    };
    Wheel addColumn(QHBoxLayout *wheels, const QString &name);
    void onChanged();
    ColorGradeValues currentValues() const;

    QCheckBox *m_enable = nullptr;
    Wheel m_lift;
    Wheel m_gamma;
    Wheel m_gain;
};
