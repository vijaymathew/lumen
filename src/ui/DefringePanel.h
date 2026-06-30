#pragma once

#include "core/DefringeNode.h" // DefringeNode::Values

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// DefringePanel is the floating tool card for chromatic-fringe suppression: an
// enable toggle plus Purple / Green amount sliders and an edge Threshold. Like
// DenoisePanel it drives a baked op — valuesChanged() triggers a debounced base
// re-bake in MainWindow. Closes on Esc/Enter.
class DefringePanel : public QWidget {
    Q_OBJECT

public:
    explicit DefringePanel(QWidget *parent = nullptr);

    void reveal(const DefringeNode::Values &values);

signals:
    void valuesChanged(const DefringeNode::Values &values);
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
    DefringeNode::Values currentValues() const;

    QPushButton *m_enable = nullptr;
    QSlider *m_purple = nullptr;
    QSlider *m_green = nullptr;
    QSlider *m_threshold = nullptr;
    QLabel *m_purpleValue = nullptr;
    QLabel *m_greenValue = nullptr;
    QLabel *m_thresholdValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
