#include "ui/CurvesPanel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kTitleHeight = 28;
constexpr int kMargin = 16;
constexpr int kPlotSize = 248;
constexpr double kHitRadius = 9.0;
constexpr double kMinGap = 0.01; // min x separation between adjacent points
} // namespace

CurvesPanel::CurvesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("curvesPanel"));
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(kMargin * 2 + kPlotSize,
                 kTitleHeight + kPlotSize + kMargin + 22);
    m_points = Curve().points(); // identity
    hide();
}

void CurvesPanel::reveal(const Curve &curve)
{
    m_points = curve.points();
    m_selected = -1;
    m_dragPoint = false;
    m_dragWindow = false;
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
    update();
}

QRect CurvesPanel::plotRect() const
{
    return QRect(kMargin, kTitleHeight, kPlotSize, kPlotSize);
}

QPointF CurvesPanel::toWidget(const QPointF &c) const
{
    const QRect pr = plotRect();
    return QPointF(pr.left() + c.x() * pr.width(),
                   pr.top() + (1.0 - c.y()) * pr.height());
}

QPointF CurvesPanel::toCurve(const QPointF &w) const
{
    const QRect pr = plotRect();
    const double x = (w.x() - pr.left()) / pr.width();
    const double y = 1.0 - (w.y() - pr.top()) / pr.height();
    return QPointF(std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0));
}

int CurvesPanel::pointAt(const QPointF &w) const
{
    for (int i = 0; i < static_cast<int>(m_points.size()); ++i) {
        if (QLineF(toWidget(m_points[i]), w).length() <= kHitRadius)
            return i;
    }
    return -1;
}

bool CurvesPanel::isEndpoint(int index) const
{
    return index == 0 || index == static_cast<int>(m_points.size()) - 1;
}

void CurvesPanel::setSelectedPosition(double x, double y)
{
    if (m_selected < 0)
        return;
    const int n = static_cast<int>(m_points.size());
    y = std::clamp(y, 0.0, 1.0);
    if (m_selected == 0)
        x = 0.0;
    else if (m_selected == n - 1)
        x = 1.0;
    else
        x = std::clamp(x, m_points[m_selected - 1].x() + kMinGap,
                       m_points[m_selected + 1].x() - kMinGap);
    m_points[m_selected] = QPointF(x, y);
    emitCurve();
    update();
}

void CurvesPanel::emitCurve()
{
    Curve c;
    c.setPoints(m_points);
    emit curveChanged(c);
}

void CurvesPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Card.
    QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(QColor(0x1c, 0x1c, 0x1f));
    p.setPen(QPen(QColor(0x38, 0x38, 0x3d)));
    p.drawRoundedRect(card, 10, 10);

    // Title.
    p.setPen(QColor(0xe8, 0xe8, 0xea));
    p.drawText(QRect(kMargin, 6, 200, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Curves"));

    const QRect pr = plotRect();
    p.fillRect(pr, QColor(0x14, 0x14, 0x16));

    // Quarter grid.
    p.setPen(QColor(0x2c, 0x2c, 0x30));
    for (int k = 1; k < 4; ++k) {
        const int x = pr.left() + pr.width() * k / 4;
        const int y = pr.top() + pr.height() * k / 4;
        p.drawLine(x, pr.top(), x, pr.bottom());
        p.drawLine(pr.left(), y, pr.right(), y);
    }
    // Diagonal reference.
    p.setPen(QColor(0x3a, 0x3a, 0x40));
    p.drawLine(pr.bottomLeft(), pr.topRight());

    // Curve.
    Curve c;
    c.setPoints(m_points);
    QPainterPath path;
    const int samples = pr.width();
    for (int i = 0; i <= samples; ++i) {
        const double x = static_cast<double>(i) / samples;
        const QPointF w = toWidget(QPointF(x, c.evaluate(x)));
        if (i == 0)
            path.moveTo(w);
        else
            path.lineTo(w);
    }
    p.setPen(QPen(QColor(0x7F, 0x77, 0xDD), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Control points.
    for (int i = 0; i < static_cast<int>(m_points.size()); ++i) {
        const QPointF w = toWidget(m_points[i]);
        if (i == m_selected) {
            p.setBrush(QColor(0xff, 0xff, 0xff));
            p.setPen(QPen(QColor(0x7F, 0x77, 0xDD), 2));
            p.drawEllipse(w, 5, 5);
        } else {
            p.setBrush(QColor(0xab, 0xa6, 0xf0));
            p.setPen(Qt::NoPen);
            p.drawEllipse(w, 4, 4);
        }
    }

    // Hint.
    p.setPen(QColor(0x8a, 0x8a, 0x90));
    p.drawText(QRect(kMargin, pr.bottom() + 4, pr.width(), 18),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("click add · drag move · drag out / Del remove"));
}

void CurvesPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    // Title bar drags the card.
    if (event->position().y() < kTitleHeight) {
        m_dragWindow = true;
        m_windowDragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    const QPointF pos = event->position();
    const int hit = pointAt(pos);
    if (hit >= 0) {
        m_selected = hit;
        m_dragPoint = true;
    } else if (plotRect().contains(pos.toPoint())) {
        // Add a new point and start dragging it.
        const QPointF c = toCurve(pos);
        auto it = std::upper_bound(m_points.begin(), m_points.end(), c,
                                   [](const QPointF &a, const QPointF &b) {
                                       return a.x() < b.x();
                                   });
        const int index = static_cast<int>(it - m_points.begin());
        m_points.insert(it, c);
        m_selected = index;
        m_dragPoint = true;
        emitCurve();
    }
    update();
}

void CurvesPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragWindow && parentWidget()) {
        const QPoint cursorInParent =
            parentWidget()->mapFromGlobal(event->globalPosition().toPoint());
        QPoint topLeft = cursorInParent - m_windowDragOffset;
        const QRect bounds = parentWidget()->rect();
        topLeft.setX(std::clamp(topLeft.x(), 0, bounds.width() - width()));
        topLeft.setY(std::clamp(topLeft.y(), 0, bounds.height() - height()));
        move(topLeft);
        return;
    }

    if (m_dragPoint && m_selected >= 0) {
        const QPointF c = toCurve(event->position());
        setSelectedPosition(c.x(), c.y());
    }
}

void CurvesPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragWindow) {
        m_dragWindow = false;
        unsetCursor();
        return;
    }

    if (m_dragPoint && m_selected >= 0) {
        // Dragging a non-endpoint point out of the plot removes it.
        if (!isEndpoint(m_selected)
            && !plotRect().contains(event->position().toPoint())) {
            m_points.erase(m_points.begin() + m_selected);
            m_selected = -1;
            emitCurve();
        }
        m_dragPoint = false;
        update();
    }
}

void CurvesPanel::keyPressEvent(QKeyEvent *event)
{
    if (m_selected >= 0) {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
            && !isEndpoint(m_selected)) {
            m_points.erase(m_points.begin() + m_selected);
            m_selected = -1;
            emitCurve();
            update();
            return;
        }
        const double step = (event->modifiers() & Qt::ShiftModifier) ? 0.05 : 0.01;
        const QPointF cur = m_points[m_selected];
        switch (event->key()) {
        case Qt::Key_Left:
            setSelectedPosition(cur.x() - step, cur.y());
            return;
        case Qt::Key_Right:
            setSelectedPosition(cur.x() + step, cur.y());
            return;
        case Qt::Key_Up:
            setSelectedPosition(cur.x(), cur.y() + step);
            return;
        case Qt::Key_Down:
            setSelectedPosition(cur.x(), cur.y() - step);
            return;
        default:
            break;
        }
    }
    QWidget::keyPressEvent(event); // let Esc etc. propagate to MainWindow
}
