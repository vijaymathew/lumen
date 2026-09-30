#pragma once

#include "ui/FloatingToolPanel.h"

#include <QColor>

class QLabel;
class QPushButton;
class QSlider;

// HealPanel is the floating tool card for the healing and retouch brushes, which
// share one brush UI (a Heal / Retouch mode switch at the top). Heal: paint over
// a blemish/object (Add) to remove it. Retouch: pick a colour from the image,
// then paint it over another region. Subtract un-marks, Clear resets. Size and
// Hardness control the brush. The actual inpainting / colour compositing happens
// on stroke end / commit in MainWindow; the painting reuses the shared brush +
// session-undo infrastructure.
class HealPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    explicit HealPanel(QWidget *parent = nullptr);

    // `retouch` selects the mode; `colour` is the current retouch colour (invalid
    // when none is picked yet) and `opacity` the node-wide retouch opacity.
    void reveal(int size, int hardness, bool add, bool highQuality, bool retouch,
                const QColor &colour, int opacity);
    // Shows the picked retouch colour on the swatch (invalid = "none picked").
    void setRetouchColour(const QColor &colour);
    // Reflect externally-changed size/hardness (e.g. s/h + wheel) without
    // re-emitting settingsChanged.
    void setBrushParams(int size, int hardness);

signals:
    void settingsChanged(int size, int hardness, bool add);
    void clearRequested();
    void qualityChanged(bool highQuality);
    void modeChanged(bool retouch);
    void pickColourRequested();
    void opacityChanged(int percent);

private:
    QSlider *addBrushRow(const QString &name, int def, QLabel **valueOut);
    void emitSettings();
    void applyMode(bool retouch);

    QPushButton *m_addButton = nullptr;
    QPushButton *m_subButton = nullptr;
    QPushButton *m_healModeButton = nullptr;
    QPushButton *m_retouchModeButton = nullptr;
    QPushButton *m_qualityButton = nullptr;
    QPushButton *m_pickButton = nullptr;
    QLabel *m_swatch = nullptr;
    QWidget *m_retouchRow = nullptr; // pick button + swatch
    QWidget *m_opacityBox = nullptr; // opacity label + slider
    QSlider *m_opacity = nullptr;
    QLabel *m_opacityValue = nullptr;
    QLabel *m_hint = nullptr;
    bool m_retouch = false;
    bool m_highQuality = true;
    QSlider *m_size = nullptr;
    QSlider *m_hardness = nullptr;
    QLabel *m_sizeValue = nullptr;
    QLabel *m_hardnessValue = nullptr;
    bool m_add = true;
};
