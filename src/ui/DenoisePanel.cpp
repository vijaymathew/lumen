#include "ui/DenoisePanel.h"

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

DenoisePanel::DenoisePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("denoisePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Denoise"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_enable = new QPushButton(QStringLiteral("Reduce noise"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &DenoisePanel::onChanged);
    layout->addWidget(m_enable);

    m_luma = addRow(QStringLiteral("Luminance"), 0, 100, &m_lumaValue);
    m_chroma = addRow(QStringLiteral("Color"), 0, 100, &m_chromaValue);

    setStyleSheet(QStringLiteral(R"(
        #denoisePanel {
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

QSlider *DenoisePanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &DenoisePanel::onChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

DenoiseNode::Values DenoisePanel::currentValues() const
{
    DenoiseNode::Values v;
    v.enabled = m_enable->isChecked();
    v.luma = static_cast<float>(m_luma->value());
    v.chroma = static_cast<float>(m_chroma->value());
    return v;
}

void DenoisePanel::refreshLabels()
{
    const DenoiseNode::Values v = currentValues();
    m_lumaValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.luma)));
    m_chromaValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.chroma)));
    const bool on = v.enabled;
    m_luma->setEnabled(on);
    m_chroma->setEnabled(on);
}

void DenoisePanel::reveal(const DenoiseNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_luma);
    const QSignalBlocker b2(m_chroma);
    m_enable->setChecked(values.enabled);
    m_luma->setValue(static_cast<int>(std::lround(values.luma)));
    m_chroma->setValue(static_cast<int>(std::lround(values.chroma)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void DenoisePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void DenoisePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void DenoisePanel::mouseMoveEvent(QMouseEvent *event)
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

void DenoisePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool DenoisePanel::eventFilter(QObject *watched, QEvent *event)
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
