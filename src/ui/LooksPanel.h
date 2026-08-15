#pragma once

#include "ui/FloatingToolPanel.h"

class QLabel;
class QPushButton;
class QSlider;

// LooksPanel is the floating tool card for 3D-LUT "looks": load a HALD CLUT and
// blend it with the image via an intensity slider. The actual file dialog and
// node updates live in MainWindow; the panel just emits intent.
class LooksPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit LooksPanel(QWidget *parent = nullptr);

    // Shows the panel seeded with the current look name and intensity ([0,1]).
    void reveal(const QString &lookName, double intensity);
    void setLookName(const QString &name);

signals:
    void loadRequested();
    void clearRequested();
    void intensityChanged(double intensity); // [0,1]

private:
    void onSliderChanged(int value);

    QLabel *m_name = nullptr;
    QSlider *m_intensity = nullptr;
    QLabel *m_intensityValue = nullptr;
};
