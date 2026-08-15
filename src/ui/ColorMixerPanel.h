#pragma once

#include "core/ColorMixerNode.h"
#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// ColorMixerPanel is the floating, draggable tool card for the per-color HSL
// mixer. A row of eight colour swatches selects the active hue band; the three
// sliders (Hue / Saturation / Luminance) below edit that band. It drives the
// live preview via valuesChanged() and closes on Esc/Enter. Tool panels float
// (DESIGN.md §4.6) — the user can drag it anywhere but the sliders/buttons.
class ColorMixerPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit ColorMixerPanel(QWidget *parent = nullptr);

    // Shows the panel seeded with the current values.
    void reveal(const ColorMixerValues &values);

signals:
    void valuesChanged(const ColorMixerValues &values);

private:
    void selectBand(int index);
    void onSliderChanged();
    void refreshLabels();

    QPushButton *m_bandBtn[8] = {};
    QLabel *m_bandLabel = nullptr; // "Editing: Red"
    QSlider *m_hue = nullptr;
    QSlider *m_sat = nullptr;
    QSlider *m_lum = nullptr;
    QLabel *m_hueValue = nullptr;
    QLabel *m_satValue = nullptr;
    QLabel *m_lumValue = nullptr;

    ColorMixerValues m_values;
    int m_selected = 0;
};
