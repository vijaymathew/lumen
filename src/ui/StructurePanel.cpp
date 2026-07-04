#include "ui/StructurePanel.h"

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

StructurePanel::StructurePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("structurePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Structure"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_enable = new QPushButton(QStringLiteral("Structure"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &StructurePanel::onChanged);
    layout->addWidget(m_enable);

    m_amount = addRow(QStringLiteral("Amount"), -100, 100, &m_amountValue);
    m_radius = addRow(QStringLiteral("Radius"), static_cast<int>(StructureNode::kMinRadius),
                      static_cast<int>(StructureNode::kMaxRadius), &m_radiusValue);

    setStyleSheet(QStringLiteral(R"(
        #structurePanel {
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

QSlider *StructurePanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &StructurePanel::onChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

StructureNode::Values StructurePanel::currentValues() const
{
    StructureNode::Values v;
    v.enabled = m_enable->isChecked();
    v.amount = static_cast<float>(m_amount->value());
    v.radius = static_cast<float>(m_radius->value());
    return v;
}

void StructurePanel::refreshLabels()
{
    const StructureNode::Values v = currentValues();
    m_amountValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.amount)));
    m_radiusValue->setText(QStringLiteral("%1 px").arg(static_cast<int>(v.radius)));
    const bool on = v.enabled;
    m_amount->setEnabled(on);
    m_radius->setEnabled(on);
}

void StructurePanel::reveal(const StructureNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_amount);
    const QSignalBlocker b2(m_radius);
    m_enable->setChecked(values.enabled);
    m_amount->setValue(static_cast<int>(std::lround(values.amount)));
    m_radius->setValue(static_cast<int>(std::lround(values.radius)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void StructurePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void StructurePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void StructurePanel::mouseMoveEvent(QMouseEvent *event)
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

void StructurePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool StructurePanel::eventFilter(QObject *watched, QEvent *event)
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
