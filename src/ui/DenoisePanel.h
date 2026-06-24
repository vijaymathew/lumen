#pragma once

#include "core/DenoiseNode.h" // DenoiseNode::Values

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// DenoisePanel is the floating tool card for noise reduction: an enable toggle,
// a Luma slider and a Chroma slider. It mirrors SharpenPanel — drives the
// preview via valuesChanged() (the base re-bakes) and closes on Esc/Enter.
class DenoisePanel : public QWidget {
    Q_OBJECT

public:
    explicit DenoisePanel(QWidget *parent = nullptr);

    void reveal(const DenoiseNode::Values &values);

signals:
    void valuesChanged(const DenoiseNode::Values &values);
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
    DenoiseNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_luma = nullptr;
    QSlider *m_chroma = nullptr;
    QLabel *m_lumaValue = nullptr;
    QLabel *m_chromaValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
