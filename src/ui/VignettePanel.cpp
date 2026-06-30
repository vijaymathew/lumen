#include "ui/VignettePanel.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

VignettePanel::VignettePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("vignettePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Vignette"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_enable = new QPushButton(QStringLiteral("Vignette"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &VignettePanel::onChanged);
    layout->addWidget(m_enable);

    m_amount = addRow(QStringLiteral("Amount"), -100, 100, &m_amountValue);
    m_midpoint = addRow(QStringLiteral("Midpoint"), 0, 100, &m_midpointValue);
    m_roundness = addRow(QStringLiteral("Roundness"), -100, 100, &m_roundnessValue);
    m_feather = addRow(QStringLiteral("Feather"), 0, 100, &m_featherValue);

    setStyleSheet(QStringLiteral(R"(
        #vignettePanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 4px 8px; font-size: 12px;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
    )"));

    hide();
}

QSlider *VignettePanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *valueLabel = new QLabel(this);
    valueLabel->setObjectName(QStringLiteral("rowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(nameLabel);
    header->addStretch(1);
    header->addWidget(valueLabel);

    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->installEventFilter(this);
    connect(slider, &QSlider::valueChanged, this, &VignettePanel::onChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

VignetteParams VignettePanel::currentValues() const
{
    VignetteParams v;
    v.enabled = m_enable->isChecked();
    v.amount = static_cast<float>(m_amount->value());
    v.midpoint = static_cast<float>(m_midpoint->value());
    v.roundness = static_cast<float>(m_roundness->value());
    v.feather = static_cast<float>(m_feather->value());
    return v;
}

void VignettePanel::refreshLabels()
{
    const VignetteParams v = currentValues();
    m_amountValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.amount)));
    m_midpointValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.midpoint)));
    m_roundnessValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.roundness)));
    m_featherValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.feather)));
    const bool on = v.enabled;
    m_amount->setEnabled(on);
    m_midpoint->setEnabled(on);
    m_roundness->setEnabled(on);
    m_feather->setEnabled(on);
}

void VignettePanel::reveal(const VignetteParams &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_amount);
    const QSignalBlocker b2(m_midpoint);
    const QSignalBlocker b3(m_roundness);
    const QSignalBlocker b4(m_feather);
    m_enable->setChecked(values.enabled);
    m_amount->setValue(static_cast<int>(std::lround(values.amount)));
    m_midpoint->setValue(static_cast<int>(std::lround(values.midpoint)));
    m_roundness->setValue(static_cast<int>(std::lround(values.roundness)));
    m_feather->setValue(static_cast<int>(std::lround(values.feather)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void VignettePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void VignettePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void VignettePanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !parentWidget())
        return;
    const QPoint cursorInParent =
        parentWidget()->mapFromGlobal(event->globalPosition().toPoint());
    QPoint topLeft = cursorInParent - m_dragOffset;
    const QRect bounds = parentWidget()->rect();
    topLeft.setX(std::clamp(topLeft.x(), 0, bounds.width() - width()));
    topLeft.setY(std::clamp(topLeft.y(), 0, bounds.height() - height()));
    move(topLeft);
}

void VignettePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool VignettePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
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
