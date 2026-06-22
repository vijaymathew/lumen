#pragma once

#include <QWidget>

class QLabel;
class QSlider;

// ExposurePanel is the bottom-docked tool panel for the exposure adjustment
// (DESIGN.md §4.4 — tool panels dock at the bottom). It drives the live preview
// via exposureChanged() and closes on Esc/commit.
class ExposurePanel : public QWidget {
    Q_OBJECT

public:
    explicit ExposurePanel(QWidget *parent = nullptr);

    // Shows the panel seeded with the current exposure (EV stops).
    void reveal(double ev);

signals:
    void exposureChanged(double ev);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onSliderChanged(int value);

    QSlider *m_slider = nullptr;
    QLabel *m_value = nullptr;
};
