#pragma once

#include <QWidget>

class QLabel;
class QSlider;

// Tone adjustment values, in the units the sliders use.
struct ToneValues {
    float exposure = 0.0f;    // EV stops
    float contrast = 0.0f;    // -100..100
    float saturation = 0.0f;  // -100..100
    float temperature = 0.0f; // -100..100 (warm + / cool -)
    float tint = 0.0f;        // -100..100 (magenta + / green -)
};

// TonePanel is the floating, draggable right-side tool card for tonal
// adjustments (exposure / contrast / saturation). It drives the live preview via
// valuesChanged() and closes on Esc/Enter. Tool panels float (DESIGN.md §4.6) —
// the user can drag it anywhere but the sliders.
class TonePanel : public QWidget {
    Q_OBJECT

public:
    explicit TonePanel(QWidget *parent = nullptr);

    // Shows the panel seeded with the current values.
    void reveal(const ToneValues &values);

signals:
    void valuesChanged(const ToneValues &values);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(const QString &name, int min, int max, QLabel **valueOut);
    void onSliderChanged();
    void refreshLabels();
    ToneValues currentValues() const;

    QSlider *m_exposure = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_saturation = nullptr;
    QSlider *m_temperature = nullptr;
    QSlider *m_tint = nullptr;
    QLabel *m_exposureValue = nullptr;
    QLabel *m_contrastValue = nullptr;
    QLabel *m_saturationValue = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_tintValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
