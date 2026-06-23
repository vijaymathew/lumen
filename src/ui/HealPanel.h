#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// HealPanel is the floating tool card for the healing brush: paint over a
// blemish/object (Add) to remove it, Subtract to un-mark, Clear to reset. Size
// and Hardness control the brush. The actual inpainting happens on stroke end /
// commit in MainWindow; the painting reuses the shared brush + session-undo
// infrastructure.
class HealPanel : public QWidget {
    Q_OBJECT

public:
    explicit HealPanel(QWidget *parent = nullptr);

    void reveal(int size, int hardness, bool add);
    // Reflect externally-changed size/hardness (e.g. s/h + wheel) without
    // re-emitting settingsChanged.
    void setBrushParams(int size, int hardness);

signals:
    void settingsChanged(int size, int hardness, bool add);
    void clearRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(const QString &name, int def, QLabel **valueOut);
    void emitSettings();

    QPushButton *m_addButton = nullptr;
    QPushButton *m_subButton = nullptr;
    QSlider *m_size = nullptr;
    QSlider *m_hardness = nullptr;
    QLabel *m_sizeValue = nullptr;
    QLabel *m_hardnessValue = nullptr;
    bool m_add = true;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
