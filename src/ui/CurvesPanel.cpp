#include "ui/CurvesPanel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int kTitleHeight = 26;
constexpr int kTabsHeight = 22;
constexpr int kMargin = 16;
constexpr int kPlotSize = 248;
constexpr int kPlotTop = kTitleHeight + kTabsHeight;
constexpr double kHitRadius = 9.0;
constexpr double kMinGap = 0.01;

const std::array<const char *, 4> kTabLabels{"RGB", "R", "G", "B"};

QColor curveColorFor(int channel)
{
    switch (channel) {
    case 1: return QColor(0xe0, 0x6b, 0x6b); // red
    case 2: return QColor(0x6b, 0xc6, 0x6b); // green
    case 3: return QColor(0x6f, 0xa8, 0xdc); // blue
    default: return QColor(0xab, 0xa6, 0xf0); // master
    }
}
} // namespace

CurvesPanel::CurvesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("curvesPanel"));
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(kMargin * 2 + kPlotSize, kPlotTop + kPlotSize + kMargin + 22);
    for (auto &pts : m_points)
        pts = Curve().points(); // identity
    hide();
}

void CurvesPanel::reveal(const ChannelCurves &curves)
{
    m_points[0] = curves.master.points();
    m_points[1] = curves.red.points();
    m_points[2] = curves.green.points();
    m_points[3] = curves.blue.points();
    m_channel = 0;
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
    return QRect(kMargin, kPlotTop, kPlotSize, kPlotSize);
}

QRect CurvesPanel::tabRect(int channel) const
{
    const int w = kPlotSize / 4;
    return QRect(kMargin + channel * w, kTitleHeight, w, kTabsHeight);
}

QColor CurvesPanel::channelColor() const
{
    return curveColorFor(m_channel);
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
    const std::vector<QPointF> &pts = m_points[m_channel];
    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        if (QLineF(toWidget(pts[i]), w).length() <= kHitRadius)
            return i;
    }
    return -1;
}

bool CurvesPanel::isEndpoint(int index) const
{
    return index == 0 || index == static_cast<int>(m_points[m_channel].size()) - 1;
}

void CurvesPanel::setSelectedPosition(double x, double y)
{
    if (m_selected < 0)
        return;
    std::vector<QPointF> &pts = m_points[m_channel];
    const int n = static_cast<int>(pts.size());
    y = std::clamp(y, 0.0, 1.0);
    if (m_selected == 0)
        x = 0.0;
    else if (m_selected == n - 1)
        x = 1.0;
    else
        x = std::clamp(x, pts[m_selected - 1].x() + kMinGap,
                       pts[m_selected + 1].x() - kMinGap);
    pts[m_selected] = QPointF(x, y);
    emitCurves();
    update();
}

void CurvesPanel::emitCurves()
{
    ChannelCurves c;
    c.master.setPoints(m_points[0]);
    c.red.setPoints(m_points[1]);
    c.green.setPoints(m_points[2]);
    c.blue.setPoints(m_points[3]);
    emit curveChanged(c);
}

void CurvesPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(QColor(0x1c, 0x1c, 0x1f));
    p.setPen(QPen(QColor(0x38, 0x38, 0x3d)));
    p.drawRoundedRect(card, 10, 10);

    p.setPen(QColor(0xe8, 0xe8, 0xea));
    p.drawText(QRect(kMargin, 5, 200, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("Curves"));

    // Channel tabs.
    for (int ch = 0; ch < 4; ++ch) {
        const QRect tr = tabRect(ch);
        const bool active = ch == m_channel;
        if (active) {
            p.setBrush(QColor(0x2a, 0x2a, 0x2e));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(tr.adjusted(2, 1, -2, -1), 5, 5);
        }
        p.setPen(active ? curveColorFor(ch) : QColor(0x8a, 0x8a, 0x90));
        p.drawText(tr, Qt::AlignCenter, QString::fromLatin1(kTabLabels[ch]));
    }

    const QRect pr = plotRect();
    p.setPen(Qt::NoPen);
    p.fillRect(pr, QColor(0x14, 0x14, 0x16));

    p.setPen(QColor(0x2c, 0x2c, 0x30));
    for (int k = 1; k < 4; ++k) {
        const int x = pr.left() + pr.width() * k / 4;
        const int y = pr.top() + pr.height() * k / 4;
        p.drawLine(x, pr.top(), x, pr.bottom());
        p.drawLine(pr.left(), y, pr.right(), y);
    }
    p.setPen(QColor(0x3a, 0x3a, 0x40));
    p.drawLine(pr.bottomLeft(), pr.topRight());

    const std::vector<QPointF> &pts = m_points[m_channel];
    Curve c;
    c.setPoints(pts);
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
    p.setPen(QPen(channelColor(), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    for (int i = 0; i < static_cast<int>(pts.size()); ++i) {
        const QPointF w = toWidget(pts[i]);
        if (i == m_selected) {
            p.setBrush(QColor(0xff, 0xff, 0xff));
            p.setPen(QPen(channelColor(), 2));
            p.drawEllipse(w, 5, 5);
        } else {
            p.setBrush(channelColor());
            p.setPen(Qt::NoPen);
            p.drawEllipse(w, 4, 4);
        }
    }

    p.setPen(QColor(0x8a, 0x8a, 0x90));
    p.drawText(QRect(kMargin, pr.bottom() + 4, pr.width(), 18),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("click add · drag move · right-click / Del remove"));
}

void CurvesPanel::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();

    // Right-click directly on a (non-endpoint) point deletes it in place — faster
    // and steadier than dragging it out of the plot. Ignored elsewhere.
    if (event->button() == Qt::RightButton) {
        const int hit = pointAt(pos);
        if (hit >= 0 && !isEndpoint(hit)) {
            m_points[m_channel].erase(m_points[m_channel].begin() + hit);
            m_selected = -1;
            emitCurves();
            update();
        }
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    // Channel tabs.
    if (pos.y() >= kTitleHeight && pos.y() < kPlotTop) {
        for (int ch = 0; ch < 4; ++ch) {
            if (tabRect(ch).contains(pos.toPoint())) {
                m_channel = ch;
                m_selected = -1;
                update();
                return;
            }
        }
    }

    // Title bar drags the card.
    if (pos.y() < kTitleHeight) {
        m_dragWindow = true;
        m_windowDragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    const int hit = pointAt(pos);
    if (hit >= 0) {
        m_selected = hit;
        m_dragPoint = true;
    } else if (plotRect().contains(pos.toPoint())) {
        const QPointF c = toCurve(pos);
        std::vector<QPointF> &pts = m_points[m_channel];
        auto it = std::upper_bound(pts.begin(), pts.end(), c,
                                   [](const QPointF &a, const QPointF &b) {
                                       return a.x() < b.x();
                                   });
        const int index = static_cast<int>(it - pts.begin());
        pts.insert(it, c);
        m_selected = index;
        m_dragPoint = true;
        emitCurves();
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
        if (!isEndpoint(m_selected)
            && !plotRect().contains(event->position().toPoint())) {
            m_points[m_channel].erase(m_points[m_channel].begin() + m_selected);
            m_selected = -1;
            emitCurves();
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
            m_points[m_channel].erase(m_points[m_channel].begin() + m_selected);
            m_selected = -1;
            emitCurves();
            update();
            return;
        }
        const double step = (event->modifiers() & Qt::ShiftModifier) ? 0.05 : 0.01;
        const QPointF cur = m_points[m_channel][m_selected];
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
