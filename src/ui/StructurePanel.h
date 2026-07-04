#pragma once

#include "core/StructureNode.h" // StructureNode::Values

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// StructurePanel is the floating tool card for local contrast ("Structure" /
// Clarity): an enable toggle, a bipolar Amount slider (negative softens) and a
// Radius slider. It mirrors SharpenPanel — drives the preview via valuesChanged()
// (the base re-bakes) and closes on Esc/Enter.
class StructurePanel : public QWidget {
    Q_OBJECT

public:
    explicit StructurePanel(QWidget *parent = nullptr);

    void reveal(const StructureNode::Values &values);

signals:
    void valuesChanged(const StructureNode::Values &values);
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
    StructureNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_amount = nullptr; // -100..100
    QSlider *m_radius = nullptr; // gaussian sigma in px (2..50)
    QLabel *m_amountValue = nullptr;
    QLabel *m_radiusValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
