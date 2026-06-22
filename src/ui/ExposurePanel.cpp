#include "ui/ExposurePanel.h"

#include "core/TuneNode.h"

#include <QKeyEvent>
#include <QLabel>
#include <QSlider>
#include <QHBoxLayout>

#include <cmath>

namespace {
// Slider works in integer hundredths of an EV stop.
constexpr int kScale = 100;
}

ExposurePanel::ExposurePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("exposurePanel"));

    auto *title = new QLabel(QStringLiteral("Exposure"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(static_cast<int>(TuneNode::kMinExposure * kScale),
                       static_cast<int>(TuneNode::kMaxExposure * kScale));
    m_slider->setSingleStep(1);
    m_slider->setPageStep(50);
    m_slider->installEventFilter(this);

    m_value = new QLabel(this);
    m_value->setObjectName(QStringLiteral("toolValue"));
    m_value->setFixedWidth(72);
    m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(14);
    layout->addWidget(title);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_value);

    setStyleSheet(QStringLiteral(R"(
        #exposurePanel {
            background: #1c1c1f;
            border-top: 1px solid #38383d;
        }
        #toolTitle { color: #b4b4b8; font-size: 12px; }
        #toolValue { color: #d6d6d9; font-size: 12px; }
    )"));

    connect(m_slider, &QSlider::valueChanged, this, &ExposurePanel::onSliderChanged);

    hide();
}

void ExposurePanel::reveal(double ev)
{
    QSignalBlocker block(m_slider); // don't emit while seeding the value
    m_slider->setValue(static_cast<int>(std::lround(ev * kScale)));
    onSliderChanged(m_slider->value()); // refresh label without emitting change
    show();
    raise();
    m_slider->setFocus(Qt::ShortcutFocusReason);
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
