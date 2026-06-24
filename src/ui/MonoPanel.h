#pragma once

#include "core/MonoNode.h" // MonoValues

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// MonoPanel is the floating, draggable tool card for monochrome conversion: an
// enable toggle, a B&W channel mixer (R/G/B), and toning (strength + hue). It
// mirrors TonePanel — drives the live preview via valuesChanged() and closes on
// Esc/Enter.
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
    QSlider *m_mixR = nullptr;
    QSlider *m_mixG = nullptr;
    QSlider *m_mixB = nullptr;
    QSlider *m_toneStrength = nullptr;
    QSlider *m_toneHue = nullptr;
    QLabel *m_mixRValue = nullptr;
    QLabel *m_mixGValue = nullptr;
    QLabel *m_mixBValue = nullptr;
    QLabel *m_toneStrengthValue = nullptr;
    QLabel *m_toneHueValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
