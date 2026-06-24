#pragma once

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;
class QWidget;

// LayersPanel is the floating layer list: each row is a layer (name + visibility
// toggle, the active one highlighted). Add/Delete manage the stack; the opacity
// slider drives the active layer. Adjustments (Tone/Curves/Looks) are routed to
// the active layer by MainWindow.
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

    // Reflects the active layer's mask in the mask controls. `maskType` matches
    // MaskSpec::Type (0=None…); `feather` 0-100; controls are disabled for Base.
    void setMaskState(int maskType, int feather, bool invert, bool isBaseActive);

signals:
    void addRequested();
    void deleteRequested();
    void layerSelected(int index);
    void visibilityToggled(int index, bool enabled);
    void opacityChanged(int percent); // active layer
    void maskTypeChanged(int maskType);  // MaskSpec::Type value
    void maskFeatherChanged(int percent);
    void maskInvertChanged(bool invert);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QVBoxLayout *m_rowsLayout = nullptr;
    QSlider *m_opacity = nullptr;
    QLabel *m_opacityValue = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QVector<QWidget *> m_rowWidgets;

    // Mask controls for the active layer.
    QWidget *m_maskSection = nullptr;
    QVector<QPushButton *> m_maskTypeButtons; // None / Gradient / Radial / Lum / Colour
    QSlider *m_feather = nullptr;
    QLabel *m_featherValue = nullptr;
    QPushButton *m_invertButton = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
