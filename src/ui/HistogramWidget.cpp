#include "ui/HistogramWidget.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {
constexpr int kWidth = 256;
constexpr int kHeight = 132;
constexpr int kPad = 8; // inset for the plot area
} // namespace

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(kWidth, kHeight);
    // Passive overlay: never intercept canvas interaction.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hide();
}

void HistogramWidget::setData(const HistogramData &data)
{
    m_data = data;
    update();
}

void HistogramWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Dark rounded card.
    p.setPen(QPen(QColor(0x38, 0x38, 0x3d), 1));
    p.setBrush(QColor(0x1c, 0x1c, 0x1f, 0xe0));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    if (!m_data.valid || m_data.peak == 0)
        return;

    const QRectF plot(kPad, kPad, width() - 2 * kPad, height() - 2 * kPad);
    // sqrt scaling lifts the shadows so faint bins remain visible.
    const double norm = std::sqrt(static_cast<double>(m_data.peak));

    const QColor colours[3] = {QColor(0xff, 0x5a, 0x5a), QColor(0x5a, 0xd0, 0x6a),
                               QColor(0x5a, 0x9a, 0xff)};
    p.setCompositionMode(QPainter::CompositionMode_Plus); // additive overlap

    for (int c = 0; c < 3; ++c) {
        QPainterPath path;
        path.moveTo(plot.left(), plot.bottom());
        for (int i = 0; i < 256; ++i) {
            const double x = plot.left() + plot.width() * (i / 255.0);
            const double h = std::sqrt(static_cast<double>(m_data.bins[c][i])) / norm;
            const double y = plot.bottom() - plot.height() * h;
            path.lineTo(x, y);
        }
        path.lineTo(plot.right(), plot.bottom());
        path.closeSubpath();

        QColor fill = colours[c];
        fill.setAlpha(110);
        p.setPen(QPen(colours[c], 1.0));
        p.setBrush(fill);
        p.drawPath(path);
    }
}
