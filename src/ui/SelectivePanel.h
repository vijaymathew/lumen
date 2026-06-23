#pragma once

#include "core/SelectiveNode.h"

#include <QColor>
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;

// SelectivePanel is the floating tool card for a selective adjustment. A mask
// mode (Luminosity range or Colour affinity) gates a tone adjustment
// (exposure/contrast/saturation). A "Show mask" toggle overlays the mask.
class SelectivePanel : public QWidget {
    Q_OBJECT

public:
    explicit SelectivePanel(QWidget *parent = nullptr);

    void reveal(const SelectiveValues &values);
    void setTargetColor(const QColor &color); // after picking from the image
    // Reflect externally-changed brush size/hardness (s/h + wheel) silently.
    void setBrushParams(int size, int hardness);

signals:
    void valuesChanged(const SelectiveValues &values);
    void maskViewChanged(int mode); // 0 off, 1 red overlay, 2 grayscale
    void pickColorRequested();
    void brushSettingsChanged(int size, int hardness, bool add);
    void brushClearRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(QVBoxLayout *layout, const QString &name, int min, int max,
                    QLabel **valueOut);
    void setMaskMode(int mode, bool emitChange);
    void cycleMaskView();
    void onChanged();
    void refreshLabels();
    SelectiveValues currentValues() const;

    int m_maskMode = 0;     // 0 luminosity, 1 colour
    int m_maskViewMode = 0; // overlay: 0 off, 1 red, 2 gray
    QColor m_target;

    QPushButton *m_lumaButton = nullptr;
    QPushButton *m_colorButton = nullptr;
    QPushButton *m_brushButton = nullptr;
    QPushButton *m_maskButton = nullptr;
    QPushButton *m_invertButton = nullptr;
    QWidget *m_lumaSection = nullptr;
    QWidget *m_colorSection = nullptr;
    QWidget *m_brushSection = nullptr;
    QLabel *m_swatch = nullptr;

    QSlider *m_brushSize = nullptr;
    QSlider *m_brushHardness = nullptr;
    QLabel *m_brushSizeValue = nullptr;
    QLabel *m_brushHardnessValue = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_subButton = nullptr;
    bool m_brushAdd = true;

    QSlider *m_low = nullptr;
    QSlider *m_high = nullptr;
    QSlider *m_feather = nullptr;
    QSlider *m_range = nullptr;
    QSlider *m_exposure = nullptr;
    QSlider *m_contrast = nullptr;
    QSlider *m_saturation = nullptr;
    QLabel *m_lowValue = nullptr;
    QLabel *m_highValue = nullptr;
    QLabel *m_featherValue = nullptr;
    QLabel *m_rangeValue = nullptr;
    QLabel *m_exposureValue = nullptr;
    QLabel *m_contrastValue = nullptr;
    QLabel *m_saturationValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
