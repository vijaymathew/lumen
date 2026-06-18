#include "ui/ColorWheel.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 6; // padding so the puck ring isn't clipped
// Per-channel hue axes (degrees), matching ColorGradeNode::chromaPush so the
// wheel's colours show the actual tint each position produces.
constexpr double kAxisDeg[3] = {90.0, 210.0, 330.0};
} // namespace

ColorWheel::ColorWheel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(96, 96);
    setCursor(Qt::CrossCursor);
}

int ColorWheel::radiusPx() const
{
    return std::min(width(), height()) / 2 - kMargin;
}

QPointF ColorWheel::puckCenter() const
{
    const double r = radiusPx();
    // +y is up on screen, so subtract for the y component.
    return {width() / 2.0 + m_x * r, height() / 2.0 - m_y * r};
}

void ColorWheel::setValue(float x, float y)
{
    m_x = std::clamp(x, -1.0f, 1.0f);
    m_y = std::clamp(y, -1.0f, 1.0f);
    update();
}

void ColorWheel::setFromPos(const QPointF &pos)
{
    const double r = radiusPx();
    if (r <= 0)
        return;
    double dx = (pos.x() - width() / 2.0) / r;
    double dy = (height() / 2.0 - pos.y()) / r; // +y up
    const double len = std::hypot(dx, dy);
    if (len > 1.0) { // clamp to the disc edge
        dx /= len;
        dy /= len;
    }
    m_x = static_cast<float>(dx);
    m_y = static_cast<float>(dy);
    update();
    emit changed(m_x, m_y);
}

void ColorWheel::rebuildDisc()
{
    const int d = std::min(width(), height());
    if (d <= 0)
        return;
    m_disc = QImage(d, d, QImage::Format_ARGB32_Premultiplied);
    m_disc.fill(Qt::transparent);
    const double r = d / 2.0 - kMargin;
    const double cx = d / 2.0, cy = d / 2.0;
    for (int py = 0; py < d; ++py) {
        auto *line = reinterpret_cast<QRgb *>(m_disc.scanLine(py));
        for (int px = 0; px < d; ++px) {
            const double dx = (px - cx) / r;
            const double dy = (cy - py) / r; // +y up
            const double rad = std::hypot(dx, dy);
            if (rad > 1.0) {
                line[px] = qRgba(0, 0, 0, 0);
                continue;
            }
            // Same luma-neutral chroma push as the node, shown as a tint over grey.
            const double hue = std::atan2(dy, dx);
            double push[3], mean = 0.0;
            for (int c = 0; c < 3; ++c) {
                push[c] = rad * std::cos(hue - kAxisDeg[c] * M_PI / 180.0);
                mean += push[c];
            }
            mean /= 3.0;
            const auto chan = [&](int c) {
                return std::clamp(static_cast<int>(std::lround((0.5 + 0.7 * (push[c] - mean)) * 255.0)),
                                  0, 255);
            };
            // Soft 1px edge so the disc isn't aliased.
            const int a = rad > 0.97 ? static_cast<int>((1.0 - (rad - 0.97) / 0.03) * 255.0) : 255;
            line[px] = qRgba(chan(0), chan(1), chan(2), std::clamp(a, 0, 255));
        }
    }
}

void ColorWheel::resizeEvent(QResizeEvent *)
{
    rebuildDisc();
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_disc.isNull())
        rebuildDisc();
    const int d = std::min(width(), height());
    p.drawImage(QPointF((width() - d) / 2.0, (height() - d) / 2.0), m_disc);

    // Puck: a white ring with a dark outline so it reads on any tint.
    const QPointF c = puckCenter();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 180), 3.0));
    p.drawEllipse(c, 6.0, 6.0);
    p.setPen(QPen(QColor(255, 255, 255, 230), 1.5));
    p.drawEllipse(c, 6.0, 6.0);
}

void ColorWheel::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        setFromPos(e->position());
}

void ColorWheel::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton)
        setFromPos(e->position());
}

void ColorWheel::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_x = m_y = 0.0f; // recenter
        update();
        emit changed(m_x, m_y);
    }
}
