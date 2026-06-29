#pragma once

#include "core/MaskSpec.h"

#include <QColor>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;

// LayersPanel is the floating layer list plus the unified mask editor for the
// active (non-Base) layer. Each row is a layer (name + visibility toggle, the
// active one highlighted); Add/Delete manage the stack; the opacity slider and
// the whole Mask section drive the active layer. Mask types: None / Gradient /
// Radial (edited on-canvas via MaskGizmo) and Luminosity / Colour / Brush
// (edited here). Tone for a masked layer is the normal Tone tool.
class LayersPanel : public QWidget {
    Q_OBJECT

public:
    struct Row {
        QString name;
        bool enabled = true;
    };

    explicit LayersPanel(QWidget *parent = nullptr);

    // Rebuilds the list. `active` is the active row; `activeOpacity` is 0-100.
    void setLayers(const QVector<Row> &rows, int active, int activeOpacity);

    // Reflects the active layer's mask + brush state. `isBaseActive` hides the
    // mask section (the Base layer has no mask). `showMode` is the overlay state
    // (0 off / 1 red / 2 gray).
    void setMaskState(const MaskSpec &mask, bool isBaseActive, int brushSize,
                      int brushHardness, bool brushAdd, int showMode);
    void setTargetColor(const QColor &color);     // after a canvas colour pick
    void setBrushParams(int size, int hardness);  // reflect s/h + wheel changes
    void setZoneCount(int n); // update only the zone shape-count label
    void resetZoneTool();     // check the Select tool button (no signal emitted)

signals:
    void addRequested();
    void deleteRequested();
    void layerSelected(int index);
    void renameRequested(int index, const QString &name);
    void visibilityToggled(int index, bool enabled);
    void opacityChanged(int percent); // active layer
    // Mask editing (all for the active layer).
    void maskTypeChanged(int maskType);  // MaskSpec::Type value
    void maskFeatherChanged(int percent);
    void maskInvertChanged(bool invert);
    void maskRangeChanged(int lowPercent, int highPercent); // luminosity
    void maskColorRangeChanged(int percent);                // colour affinity
    void maskPickColorRequested();
    void maskShowChanged(int mode); // overlay: 0 off, 1 red, 2 gray
    void brushSettingsChanged(int size, int hardness, bool add);
    void brushClearRequested();
    // Exclusive-zone editing (active layer). Tool: 0 select, 1 rect, 2 oval,
    // 3 circle, 4 freehand.
    void zoneToolChanged(int tool);
    void zoneModeChanged(bool subtract);
    void zoneFeatherChanged(int percent);
    void zoneClearRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // Catches a double-click on a layer-name button to start inline renaming.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QSlider *addSlider(QVBoxLayout *layout, const QString &name, int min, int max,
                       QLabel **valueOut);
    void emitBrush();
    // Swaps a layer-name button for an inline QLineEdit; commits via renameRequested.
    void beginRename(QPushButton *nameButton, int index);

    QVBoxLayout *m_rowsLayout = nullptr;
    QSlider *m_opacity = nullptr;
    QLabel *m_opacityValue = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QVector<QWidget *> m_rowWidgets;

    // Mask controls for the active layer.
    QWidget *m_maskSection = nullptr;
    QVector<QPushButton *> m_maskTypeButtons; // None/Gradient/Radial/Lum/Colour/Brush
    QPushButton *m_showButton = nullptr;
    int m_showMode = 0;

    QWidget *m_lumSection = nullptr;
    QSlider *m_low = nullptr;
    QSlider *m_high = nullptr;
    QLabel *m_lowValue = nullptr;
    QLabel *m_highValue = nullptr;

    QWidget *m_colorSection = nullptr;
    QSlider *m_range = nullptr;
    QLabel *m_rangeValue = nullptr;
    QLabel *m_swatch = nullptr;

    QWidget *m_brushSection = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_subButton = nullptr;
    QSlider *m_brushSize = nullptr;
    QSlider *m_brushHardness = nullptr;
    QLabel *m_brushSizeValue = nullptr;
    QLabel *m_brushHardnessValue = nullptr;
    bool m_brushAdd = true;

    QWidget *m_featherRow = nullptr;
    QSlider *m_feather = nullptr;
    QLabel *m_featherValue = nullptr;
    QPushButton *m_invertButton = nullptr;

    // Exclusive-zone sub-section.
    QWidget *m_zoneSection = nullptr;
    QVector<QPushButton *> m_zoneToolButtons; // Select/Rect/Oval/Circle/Freehand
    QPushButton *m_zoneAddButton = nullptr;
    QPushButton *m_zoneSubButton = nullptr;
    QSlider *m_zoneFeather = nullptr;
    QLabel *m_zoneFeatherValue = nullptr;
    QLabel *m_zoneCount = nullptr;
    bool m_zoneSubtract = false;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
