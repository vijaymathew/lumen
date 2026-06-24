#pragma once

#include "core/Histogram.h"

#include <QWidget>

// HistogramWidget is a small, non-interactive overlay that draws the RGB
// histogram as three additively-blended channel curves on a dark card. It is a
// passive consumer of the pipeline: MainWindow recomputes the data as edits
// settle and pushes it via setData().
class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    void setData(const HistogramData &data);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    HistogramData m_data;
};
