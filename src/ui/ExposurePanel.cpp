#include "ui/ExposurePanel.h"

#include "core/TuneNode.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// Slider works in integer hundredths of an EV stop.
constexpr int kScale = 100;
constexpr int kPanelWidth = 248;
}

ExposurePanel::ExposurePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("exposurePanel"));
    // A bare QWidget needs this to paint its stylesheet background (otherwise
    // the card is transparent — same gotcha as the command palette).
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Exposure"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_value = new QLabel(this);
    m_value->setObjectName(QStringLiteral("toolValue"));
    m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Header row: title on the left, current value on the right.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_value);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(static_cast<int>(TuneNode::kMinExposure * kScale),
                       static_cast<int>(TuneNode::kMaxExposure * kScale));
    m_slider->setSingleStep(1);
    m_slider->setPageStep(50);
    m_slider->installEventFilter(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);
    layout->addLayout(header);
    layout->addWidget(m_slider);

    // Floating card styling.
    setStyleSheet(QStringLiteral(R"(
        #exposurePanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #toolValue { color: #b4b4b8; font-size: 12px; }
    )"));

    connect(m_slider, &QSlider::valueChanged, this, &ExposurePanel::onSliderChanged);

    hide();
}

void ExposurePanel::reveal(double ev)
{
    QSignalBlocker block(m_slider); // don't emit while seeding the value
    m_slider->setValue(static_cast<int>(std::lround(ev * kScale)));
    onSliderChanged(m_slider->value()); // refresh label without emitting change
    adjustSize(); // size to content so nothing is clipped
    show();
    raise();
    m_slider->setFocus(Qt::ShortcutFocusReason);
}

void ExposurePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ExposurePanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !parentWidget())
        return;

    const QPoint cursorInParent =
        parentWidget()->mapFromGlobal(event->globalPosition().toPoint());
    QPoint topLeft = cursorInParent - m_dragOffset;

    // Keep the whole card inside the parent so it can't be dragged off-screen.
    const QRect bounds = parentWidget()->rect();
    topLeft.setX(std::clamp(topLeft.x(), 0, bounds.width() - width()));
    topLeft.setY(std::clamp(topLeft.y(), 0, bounds.height() - height()));
    move(topLeft);
}

void ExposurePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

void ExposurePanel::onSliderChanged(int value)
{
    const double ev = static_cast<double>(value) / kScale;
    m_value->setText(QStringLiteral("%1%2 EV")
                         .arg(ev >= 0 ? QStringLiteral("+") : QString())
                         .arg(ev, 0, 'f', 2));
    // Only emit if the slider has signals enabled (reveal() blocks them).
    if (m_slider->signalsBlocked())
        return;
    emit exposureChanged(ev);
}

bool ExposurePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_slider && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit closed();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
