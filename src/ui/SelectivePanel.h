#pragma once

#include "core/SelectiveNode.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// SelectivePanel is the floating tool card for a selective (luminosity-masked)
// tone adjustment: a Mask section (range low/high + feather) and an Adjust
// section (exposure/contrast/saturation). Drives the live preview.
class SelectivePanel : public QWidget {
    Q_OBJECT

public:
    explicit SelectivePanel(QWidget *parent = nullptr);

    void reveal(const SelectiveValues &values);

signals:
    void valuesChanged(const SelectiveValues &values);
    void maskViewChanged(int mode); // 0 off, 1 red overlay, 2 grayscale

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(const QString &name, int min, int max, QLabel **valueOut);
    void addSection(const QString &name);
    void onSliderChanged();
    void cycleMaskView();
    void refreshLabels();
    SelectiveValues currentValues() const;

    QPushButton *m_maskButton = nullptr;
    int m_maskMode = 0;

    QSlider *m_low = nullptr;
    QSlider *m_high = nullptr;
    QSlider *m_feather = nullptr;
    QSlider *m_exposure = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_saturation = nullptr;
    QLabel *m_lowValue = nullptr;
    QLabel *m_highValue = nullptr;
    QLabel *m_featherValue = nullptr;
    QLabel *m_exposureValue = nullptr;
    QLabel *m_contrastValue = nullptr;
    QLabel *m_saturationValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
