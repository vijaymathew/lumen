#include "ui/DefringePanel.h"

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

DefringePanel::DefringePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("defringePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Defringe"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_enable = new QPushButton(QStringLiteral("Remove fringing"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &DefringePanel::onChanged);
    layout->addWidget(m_enable);

    m_purple = addRow(QStringLiteral("Purple"), 0, 100, &m_purpleValue);
    m_green = addRow(QStringLiteral("Green"), 0, 100, &m_greenValue);
    m_threshold = addRow(QStringLiteral("Threshold"), 0, 100, &m_thresholdValue);

    setStyleSheet(QStringLiteral(R"(
        #defringePanel {
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

QSlider *DefringePanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &DefringePanel::onChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

DefringeNode::Values DefringePanel::currentValues() const
{
    DefringeNode::Values v;
    v.enabled = m_enable->isChecked();
    v.purple = static_cast<float>(m_purple->value());
    v.green = static_cast<float>(m_green->value());
    v.threshold = static_cast<float>(m_threshold->value());
    return v;
}

void DefringePanel::refreshLabels()
{
    const DefringeNode::Values v = currentValues();
    m_purpleValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.purple)));
    m_greenValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.green)));
    m_thresholdValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.threshold)));
    const bool on = v.enabled;
    m_purple->setEnabled(on);
    m_green->setEnabled(on);
    m_threshold->setEnabled(on);
}

void DefringePanel::reveal(const DefringeNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_purple);
    const QSignalBlocker b2(m_green);
    const QSignalBlocker b3(m_threshold);
    m_enable->setChecked(values.enabled);
    m_purple->setValue(static_cast<int>(std::lround(values.purple)));
    m_green->setValue(static_cast<int>(std::lround(values.green)));
    m_threshold->setValue(static_cast<int>(std::lround(values.threshold)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void DefringePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void DefringePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void DefringePanel::mouseMoveEvent(QMouseEvent *event)
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

void DefringePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool DefringePanel::eventFilter(QObject *watched, QEvent *event)
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
