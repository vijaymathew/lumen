#pragma once

#include "core/MonoNode.h" // MonoValues

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// MonoPanel is the floating, draggable tool card for monochrome conversion: an
// enable toggle, filter presets, an 8-color B&W mixer (how each colour renders
// as a tone), and toning (strength + hue). It mirrors TonePanel — drives the
// live preview via valuesChanged() and closes on Esc/Enter.
class MonoPanel : public QWidget {
    Q_OBJECT

public:
    explicit MonoPanel(QWidget *parent = nullptr);

    void reveal(const MonoValues &values);

signals:
    void valuesChanged(const MonoValues &values);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(const QString &name, int min, int max, QLabel **valueOut);
    void onChanged();
    void refreshLabels();
    MonoValues currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_band[8] = {};        // per-color B&W mix (Red…Magenta)
    QLabel *m_bandValue[8] = {};
    QSlider *m_toneStrength = nullptr;
    QSlider *m_toneHue = nullptr;
    QLabel *m_toneStrengthValue = nullptr;
    QLabel *m_toneHueValue = nullptr;
    // Applies an 8-band preset to the sliders and pushes the change.
    void applyPreset(const float bands[8]);

    bool m_dragging = false;
    QPoint m_dragOffset;
};
